#import <Foundation/Foundation.h>
#include "../../bridge/AudioDeviceRouting.hpp"
#include <iostream>
int main() { @autoreleasepool {
 NSData* data=[NSJSONSerialization dataWithJSONObject:lvt::devices::available() options:NSJSONWritingPrettyPrinted error:nil];
 if(!data)return 1;
 std::cout<<std::string((const char*)data.bytes,data.length)<<std::endl;
} }
