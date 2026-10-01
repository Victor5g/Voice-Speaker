#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface TranslationEngineBridge : NSObject
@property(nonatomic, readonly) BOOL running;
@property(nonatomic, readonly) BOOL voiceProcessingEnabled;
+ (NSArray<NSDictionary<NSString *, id> *> *)availableAudioDevices;
- (void)configureInputDeviceUID:(NSString *)inputUID outputDeviceUID:(NSString *)outputUID sourceLanguage:(NSString *)language;
- (BOOL)startWithError:(NSError * _Nullable * _Nullable)error;
- (void)stop;
- (void)requestStop;
- (void)playTestTone;
- (void)configureASRModel:(NSString *)path sha256:(NSString *)sha256;
- (void)configureASRBackend:(NSString *)backend;
- (BOOL)configureProcessingOptions:(NSString *)json error:(NSError * _Nullable * _Nullable)error;
- (void)configureConversationResources:(NSString *)path;
- (NSArray<NSDictionary<NSString *, id> *> *)drainConversationEvents;
- (NSArray<NSDictionary<NSString *, id> *> *)drainTranscripts;
- (NSDictionary<NSString *, id> *)snapshot;
@end
NS_ASSUME_NONNULL_END
