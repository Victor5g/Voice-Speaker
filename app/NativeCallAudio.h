#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Owns only the Core Audio objects created by this instance. No driver installation.
@interface NativeCallAudioSession : NSObject
@property(nonatomic, readonly) BOOL active;
@property(nonatomic, readonly, copy) NSString* outputDeviceUID;
+ (NSString*) microphoneUID;
+ (NSString*) speakersUID;
- (BOOL)prepareWithOutputDeviceUID:(NSString*)uid error:(NSError* _Nullable* _Nullable)error;
- (void)close;
@end
NS_ASSUME_NONNULL_END
