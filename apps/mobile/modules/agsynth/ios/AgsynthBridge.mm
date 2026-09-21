#import "AgsynthBridge.h"
#import <ExpoModulesCore/EXJavaScriptRuntime.h>
#include "jsi/jsi/AgsynthHostObject.h"

@implementation AgsynthBridge

+ (BOOL)install:(nullable id)appContext {
    if (!appContext) {
        return NO;
    }

    facebook::jsi::Runtime *jsiRuntime = nullptr;

    if ([appContext respondsToSelector:@selector(_runtime)]) {
        id runtimeObj = [appContext valueForKey:@"_runtime"];
        if (runtimeObj && [runtimeObj isKindOfClass:[EXJavaScriptRuntime class]]) {
            jsiRuntime = [(EXJavaScriptRuntime *)runtimeObj get];
        }
    }

    if (!jsiRuntime && [appContext respondsToSelector:@selector(reactBridge)]) {
        id bridgeObj = [appContext valueForKey:@"reactBridge"];
        if (bridgeObj && [bridgeObj respondsToSelector:@selector(runtime)]) {
            typedef void *(*RuntimeFunc)(id, SEL);
            SEL sel = @selector(runtime);
            RuntimeFunc func = (RuntimeFunc)[bridgeObj methodForSelector:sel];
            if (func) {
                void *r = func(bridgeObj, sel);
                jsiRuntime = reinterpret_cast<facebook::jsi::Runtime *>(r);
            }
        }
    }

    if (!jsiRuntime) {
        NSLog(@"[AgsynthBridge] Failed to find jsi::Runtime from appContext");
        return NO;
    }

    NSLog(@"[AgsynthBridge] Successfully installing AgsynthHost into jsi::Runtime");
    agentic_synth::jsi::installAgsynthHost(*jsiRuntime);
    return YES;
}

@end
