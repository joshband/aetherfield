#import <AudioToolbox/AudioToolbox.h>
#import "AetherfieldAudioUnit.h"

@interface AetherfieldAudioUnitFactoryImpl : NSObject <AUAudioUnitFactory>
@end

@implementation AetherfieldAudioUnitFactoryImpl

- (AUAudioUnit *)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc
                                                    error:(NSError **)error {
    NSLog(@"[Aetherfield Factory] createAudioUnitWithComponentDescription called");
    NSLog(@"  desc.componentType: %c%c%c%c",
          (desc.componentType >> 24) & 0xFF,
          (desc.componentType >> 16) & 0xFF,
          (desc.componentType >> 8) & 0xFF,
          desc.componentType & 0xFF);
    NSLog(@"  desc.componentSubType: %c%c%c%c",
          (desc.componentSubType >> 24) & 0xFF,
          (desc.componentSubType >> 16) & 0xFF,
          (desc.componentSubType >> 8) & 0xFF,
          desc.componentSubType & 0xFF);
    AUAudioUnit *unit = [[AetherfieldAudioUnit alloc] initWithComponentDescription:desc options:0 error:error];
    if (unit == nil) {
        NSLog(@"[Aetherfield Factory] Failed to create AUAudioUnit. Error: %@", error ? *error : @"(no error provided)");
    } else {
        NSLog(@"[Aetherfield Factory] Successfully created AUAudioUnit: %@", unit);
    }
    return unit;
}

// AUAudioUnitFactory inherits NSExtensionRequestHandling, which requires
// this method. The system never invokes an AU factory through the generic
// extension request/response path (com.apple.AudioUnit extensions are
// instantiated via createAudioUnitWithComponentDescription:error: above),
// so this is an intentional no-op -- matching Apple's own AUv3 templates,
// which stub it for the same reason.
- (void)beginRequestWithExtensionContext:(NSExtensionContext *)context {
}

@end

// C factory function for macOS AU discovery (called via dlsym by AudioComponentRegistrar).
// This allows the AudioComponents entry in Info.plist to reference "AetherfieldAudioUnitFactory"
// as a C symbol.
extern "C" AUAudioUnit* AetherfieldAudioUnitFactory(AudioComponentDescription inDesc, NSError** outError) {
    NSLog(@"[Aetherfield C Factory] Creating AU via C wrapper");
    AetherfieldAudioUnitFactoryImpl *factory = [[AetherfieldAudioUnitFactoryImpl alloc] init];
    AUAudioUnit *unit = [factory createAudioUnitWithComponentDescription:inDesc error:outError];
    return unit;
}
