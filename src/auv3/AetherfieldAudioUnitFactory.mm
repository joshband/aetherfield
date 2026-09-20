#import <AudioToolbox/AudioToolbox.h>
#import "AetherfieldAudioUnit.h"

@interface AetherfieldAudioUnitFactory : NSObject <AUAudioUnitFactory>
@end

@implementation AetherfieldAudioUnitFactory

- (AUAudioUnit *)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc
                                                    error:(NSError **)error {
    return [[AetherfieldAudioUnit alloc] initWithComponentDescription:desc options:0 error:error];
}

@end
