// myinput: runs a macOS app with the MyInput hook loaded into it.
// The counterpart of myinput_inject.exe. Where Windows injects a DLL into a suspended process,
// macOS has DYLD_INSERT_LIBRARIES, which dyld only honours for apps that aren't locked down by
// the hardened runtime. For those (most downloaded apps), we run a re-signed private copy instead.
#import <AppKit/AppKit.h>
#import <Security/Security.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>

static const int PatchVersion = 1; // bump to force existing patched copies to be rebuilt

[[noreturn]] static void Die(NSString *message) {
    fprintf(stderr, "myinput: %s\n", message.UTF8String);
    exit(1);
}

static void Usage() {
    printf("Usage: myinput [options] <app> [app arguments...]\n"
           "\n"
           "Runs <app> with MyInput, which maps the keyboard and mouse to a virtual gamepad inside that app.\n"
           "\n"
           "  <app>              an .app bundle, an executable, or the name of an app in /Applications\n"
           "  --config <name>    use the config <name>.ini instead of the one named after the app's executable\n"
           "  --background       don't bring the app to the front\n"
           "  --help             show this help\n"
           "\n"
           "Configs:  ~/Library/Application Support/MyInput/Configs\n"
           "Logs:     ~/Library/Logs/MyInput\n");
}

static NSString *SupportDir() {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/MyInput"];
}

static NSString *SelfDir() {
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        Die(@"can't find my own location");
    }
    return [@(buffer) stringByResolvingSymlinksInPath].stringByDeletingLastPathComponent;
}

static int Run(NSString *tool, NSArray<NSString *> *args, NSString **output = nil) {
    NSTask *task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:tool];
    task.arguments = args;
    NSPipe *pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;

    NSError *error;
    if (![task launchAndReturnError:&error]) {
        Die([NSString stringWithFormat:@"can't run %@: %@", tool, error.localizedDescription]);
    }
    NSData *data = [pipe.fileHandleForReading readDataToEndOfFile];
    [task waitUntilExit];
    if (output) {
        *output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    }
    return task.terminationStatus;
}

// ---- Finding the app ----

struct Target {
    NSString *Bundle;     // nil for a bare executable
    NSString *Executable; // absolute
};

static NSString *BundleContaining(NSString *executable) {
    // <X>.app/Contents/MacOS/<executable>
    NSString *macOS = executable.stringByDeletingLastPathComponent;
    NSString *contents = macOS.stringByDeletingLastPathComponent;
    NSString *bundle = contents.stringByDeletingLastPathComponent;
    bool matches = [macOS.lastPathComponent isEqualToString:@"MacOS"] &&
                   [contents.lastPathComponent isEqualToString:@"Contents"] &&
                   [bundle.pathExtension isEqualToString:@"app"];
    return matches ? bundle : nil;
}

static NSString *FindAppByName(NSString *name) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *wanted = [name.pathExtension isEqualToString:@"app"] ? name : [name stringByAppendingPathExtension:@"app"];
    for (NSString *dir in @[@"/Applications", [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]]) {
        for (NSString *entry in [fm contentsOfDirectoryAtPath:dir error:nil]) {
            if ([entry caseInsensitiveCompare:wanted] == NSOrderedSame) {
                return [dir stringByAppendingPathComponent:entry];
            }
        }
    }
    return nil;
}

static Target ResolveTarget(NSString *arg) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *path = arg.stringByExpandingTildeInPath;
    if (!path.isAbsolutePath) {
        path = [fm.currentDirectoryPath stringByAppendingPathComponent:path];
    }
    path = path.stringByStandardizingPath;

    BOOL isDir = NO;
    if (![fm fileExistsAtPath:path isDirectory:&isDir]) {
        path = FindAppByName(arg);
        if (!path) {
            Die([NSString stringWithFormat:@"can't find \"%@\" as a path or in /Applications", arg]);
        }
        isDir = YES;
    }

    Target target = {};
    if (isDir) {
        NSString *executable = [NSBundle bundleWithPath:path].executablePath;
        if (!executable) {
            Die([NSString stringWithFormat:@"%@ isn't an app bundle", path]);
        }
        target.Bundle = path;
        target.Executable = executable;
    } else {
        target.Bundle = BundleContaining(path);
        target.Executable = path;
    }
    return target;
}

// ---- Deciding whether dyld will let the hook in ----

static NSDictionary *SigningInfo(NSString *path) {
    SecStaticCodeRef code = nullptr;
    if (SecStaticCodeCreateWithPath((__bridge CFURLRef)[NSURL fileURLWithPath:path], kSecCSDefaultFlags, &code) != errSecSuccess) {
        return nil;
    }
    CFDictionaryRef info = nullptr;
    OSStatus status = SecCodeCopySigningInformation(code, kSecCSSigningInformation, &info);
    CFRelease(code);
    return status == errSecSuccess ? CFBridgingRelease(info) : nil;
}

static bool AcceptsInsertedLibraries(NSDictionary *info) {
    uint32_t flags = [info[(__bridge NSString *)kSecCodeInfoFlags] unsignedIntValue];
    NSDictionary *entitlements = info[(__bridge NSString *)kSecCodeInfoEntitlementsDict];

    if (flags & kSecCodeSignatureRestrict) {
        return false;
    }
    if (flags & kSecCodeSignatureRuntime) {
        // The hardened runtime drops DYLD_* variables and refuses libraries signed by anyone else,
        // unless the app opted out of both.
        return [entitlements[@"com.apple.security.cs.allow-dyld-environment-variables"] boolValue] &&
               [entitlements[@"com.apple.security.cs.disable-library-validation"] boolValue];
    }
    return !(flags & kSecCodeSignatureLibraryValidation);
}

// ---- The re-signed copy ----

static NSDictionary *Stamp(const Target &source) {
    struct stat info = {};
    stat(source.Executable.fileSystemRepresentation, &info);
    NSDictionary *plist = [NSBundle bundleWithPath:source.Bundle].infoDictionary;
    return @{
        @"patchVersion" : @(PatchVersion),
        @"source" : source.Bundle,
        @"version" : plist[@"CFBundleVersion"] ?: @"",
        @"shortVersion" : plist[@"CFBundleShortVersionString"] ?: @"",
        @"executableSize" : @(info.st_size),
        @"executableModified" : @(info.st_mtimespec.tv_sec),
    };
}

static NSString *PatchedCopy(const Target &source, NSDictionary *signingInfo) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *appsDir = [SupportDir() stringByAppendingPathComponent:@"Apps"];
    NSString *copy = [appsDir stringByAppendingPathComponent:source.Bundle.lastPathComponent];
    NSString *stampPath = [copy.stringByDeletingPathExtension stringByAppendingPathExtension:@"plist"];

    NSDictionary *stamp = Stamp(source);
    if ([fm fileExistsAtPath:copy] && [[NSDictionary dictionaryWithContentsOfFile:stampPath] isEqual:stamp]) {
        return copy;
    }

    fprintf(stderr, "myinput: preparing a copy of %s that accepts the hook (once per app version)...\n",
            source.Bundle.lastPathComponent.UTF8String);
    [fm createDirectoryAtPath:appsDir withIntermediateDirectories:YES attributes:nil error:nil];
    [fm removeItemAtPath:copy error:nil];
    [fm removeItemAtPath:stampPath error:nil];

    // -c clones on APFS, so the copy takes next to no disk space.
    NSString *output;
    if (Run(@"/bin/cp", @[@"-cR", source.Bundle, copy], &output) != 0) {
        Die([NSString stringWithFormat:@"couldn't copy the app: %@", output]);
    }
    // The copy is about to get a new signature, made here by the user running it. Left quarantined,
    // Gatekeeper would refuse it for no longer matching the developer's notarized original.
    Run(@"/usr/bin/xattr", @[@"-dr", @"com.apple.quarantine", copy]);

    // Keep what the app was allowed to do (sandbox-free entitlements such as microphone access),
    // drop what only its developer's signature can carry, and open the door for the hook.
    NSMutableDictionary *entitlements = [NSMutableDictionary dictionary];
    NSDictionary *original = signingInfo[(__bridge NSString *)kSecCodeInfoEntitlementsDict];
    for (NSString *key in original) {
        if ([key hasPrefix:@"com.apple.security."] && ![key isEqualToString:@"com.apple.security.app-sandbox"]) {
            entitlements[key] = original[key];
        }
    }
    entitlements[@"com.apple.security.cs.allow-dyld-environment-variables"] = @YES;
    entitlements[@"com.apple.security.cs.disable-library-validation"] = @YES;

    NSString *entitlementsPath = [appsDir stringByAppendingPathComponent:@".entitlements.plist"];
    [entitlements writeToFile:entitlementsPath atomically:YES];

    // An ad-hoc signature ("-") without the hardened runtime. Only the main executable is re-signed;
    // the frameworks inside keep their original signatures.
    int status = Run(@"/usr/bin/codesign", @[@"--force", @"--sign", @"-", @"--entitlements", entitlementsPath, copy], &output);
    [fm removeItemAtPath:entitlementsPath error:nil];
    if (status != 0) {
        [fm removeItemAtPath:copy error:nil];
        Die([NSString stringWithFormat:@"couldn't re-sign the copy: %@", output]);
    }

    [stamp writeToFile:stampPath atomically:YES];
    return copy;
}

// ---- Configs ----

static void InstallConfigs(NSString *selfDir) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *shipped = [selfDir stringByAppendingPathComponent:@"Configs"];
    NSString *installed = [SupportDir() stringByAppendingPathComponent:@"Configs"];
    [fm createDirectoryAtPath:installed withIntermediateDirectories:YES attributes:nil error:nil];

    // Never overwrite: these are the user's to edit.
    for (NSString *name in [fm contentsOfDirectoryAtPath:shipped error:nil]) {
        NSString *dest = [installed stringByAppendingPathComponent:name];
        if ([name.pathExtension isEqualToString:@"ini"] && ![fm fileExistsAtPath:dest]) {
            [fm copyItemAtPath:[shipped stringByAppendingPathComponent:name] toPath:dest error:nil];
        }
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        NSString *config;
        bool background = false;
        int argIdx = 1;
        for (; argIdx < argc; argIdx++) {
            NSString *arg = @(argv[argIdx]);
            if ([arg isEqualToString:@"--help"] || [arg isEqualToString:@"-h"]) {
                Usage();
                return 0;
            } else if ([arg isEqualToString:@"--config"] && argIdx + 1 < argc) {
                config = @(argv[++argIdx]);
            } else if ([arg isEqualToString:@"--background"]) {
                background = true;
            } else if ([arg hasPrefix:@"--"]) {
                Die([NSString stringWithFormat:@"unknown option %@ (see --help)", arg]);
            } else {
                break;
            }
        }
        if (argIdx >= argc) {
            Usage();
            return 1;
        }

        NSString *selfDir = SelfDir();
        NSString *hook = [selfDir stringByAppendingPathComponent:@"libmyinput_hook.dylib"];
        if (![NSFileManager.defaultManager fileExistsAtPath:hook]) {
            Die([NSString stringWithFormat:@"%@ is missing", hook]);
        }

        Target target = ResolveTarget(@(argv[argIdx]));
        NSString *executable = target.Executable;

        NSDictionary *signingInfo = SigningInfo(target.Bundle ?: target.Executable);
        if (signingInfo && !AcceptsInsertedLibraries(signingInfo)) {
            if (!target.Bundle) {
                Die(@"this executable is protected by the hardened runtime and isn't inside an .app, so it can't be prepared for the hook");
            }
            if ([target.Bundle hasPrefix:@"/System/"]) {
                Die(@"apps that ship with macOS can't be hooked");
            }
            NSString *copy = PatchedCopy(target, signingInfo);
            executable = [NSBundle bundleWithPath:copy].executablePath;
            if (!executable) {
                Die(@"the prepared copy of the app is broken, delete ~/Library/Application Support/MyInput/Apps and retry");
            }
        }

        InstallConfigs(selfDir);

        NSString *configName = config ?: executable.lastPathComponent.stringByDeletingPathExtension;
        fprintf(stderr, "myinput: running %s with config %s.ini (falls back to _default.ini), log in ~/Library/Logs/MyInput/%s.log\n",
                (target.Bundle ?: target.Executable).lastPathComponent.UTF8String, configName.UTF8String,
                executable.lastPathComponent.stringByDeletingPathExtension.UTF8String);

        NSString *bundle = BundleContaining(executable);
        if (bundle) {
            // Start it the way Finder would, so it is a regular app that outlives this command.
            NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
            NSMutableDictionary *environment = [NSMutableDictionary dictionaryWithObject:hook forKey:@"DYLD_INSERT_LIBRARIES"];
            environment[@"MYINPUT_HOOK_CONFIG"] = config;
            configuration.environment = environment;
            configuration.createsNewApplicationInstance = YES;
            configuration.activates = !background;
            NSMutableArray *arguments = [NSMutableArray array];
            for (int i = argIdx + 1; i < argc; i++) {
                [arguments addObject:@(argv[i])];
            }
            configuration.arguments = arguments;

            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            __block NSError *launchError;
            [NSWorkspace.sharedWorkspace openApplicationAtURL:[NSURL fileURLWithPath:bundle]
                                                configuration:configuration
                                            completionHandler:^(NSRunningApplication *app, NSError *error) {
                                                launchError = error;
                                                dispatch_semaphore_signal(done);
                                            }];
            dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
            if (launchError) {
                Die([NSString stringWithFormat:@"couldn't start %@: %@", bundle, launchError.localizedDescription]);
            }
            return 0;
        }

        // A bare executable: become it.
        setenv("DYLD_INSERT_LIBRARIES", hook.fileSystemRepresentation, 1);
        if (config) {
            setenv("MYINPUT_HOOK_CONFIG", config.UTF8String, 1);
        }

        argv[argIdx] = (char *)executable.fileSystemRepresentation;
        execv(argv[argIdx], argv + argIdx);
        Die([NSString stringWithFormat:@"couldn't start %@: %s", executable, strerror(errno)]);
    }
}
