#import <Foundation/Foundation.h>

// Keyed/Indexed subscripting is a 10.8+ Foundation feature; the CDM's
// modern ObjC uses dict[key] = obj and arr[i] syntax, which the old
// runtimes resolve to these selectors. Reimplement Apple's own legacy
// shim category so the messages land.

@implementation NSDictionary (LegacySubscripting)
- (id)objectForKeyedSubscript:(id)key {
  return [self objectForKey:key];
}
@end

@implementation NSMutableDictionary (LegacySubscripting)
- (void)setObject:(id)obj forKeyedSubscript:(id<NSCopying>)key {
  if (obj) {
    [self setObject:obj forKey:key];
  } else {
    [self removeObjectForKey:key];
  }
}
@end

@implementation NSArray (LegacySubscripting)
- (id)objectAtIndexedSubscript:(NSUInteger)idx {
  return [self objectAtIndex:idx];
}
@end

@implementation NSMutableArray (LegacySubscripting)
- (void)setObject:(id)obj atIndexedSubscript:(NSUInteger)idx {
  // Match NSMutableArray semantics: out-of-range indices raise, they are
  // never silently dropped.
  [self replaceObjectAtIndex:idx withObject:obj];
}
@end
