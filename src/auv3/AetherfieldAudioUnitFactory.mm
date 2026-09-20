#import <AudioToolbox/AudioToolbox.h>
#import "AetherfieldAudioUnit.h"

@interface AetherfieldAudioUnitFactory : NSObject <AUAudioUnitFactory>
@end

@implementation AetherfieldAudioUnitFactory

- (AUAudioUnit *)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc
                                                    error:(NSError **)error {
    return [[AetherfieldAudioUnit alloc] initWithComponentDescription:desc options:0 error:error];
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
