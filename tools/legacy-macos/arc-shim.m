/* ARC runtime entry points for macOS 10.6, whose libobjc predates ARC, that
   compat-10.6.c does not already provide (the shared entry points live there
   and reach every link via the force-loaded compat object). Packed into
   libpfcompat106.a. Weak references are emulated with a strong retain, as in
   compat-10.6.c: 10.6 has no weak runtime and clang refuses __weak at this
   deployment target, so these only serve manual runtime callers. */

#import <Foundation/Foundation.h>
#import <objc/objc.h>

id objc_autoreleaseReturnValue(id obj) {
  return obj ? [obj autorelease] : nil;
}

id objc_retainAutoreleaseReturnValue(id obj) {
  return obj ? [[obj retain] autorelease] : nil;
}

void objc_storeStrong(id* location, id obj) {
  id prev = *location;
  *location = obj ? [obj retain] : nil;
  if (prev) [prev release];
}

void objc_copyWeak(id* to, id* from) {
  if (*to) [*to release];
  *to = *from ? [*from retain] : nil;
}

id objc_loadWeak(id* location) {
  id obj = *location;
  return obj ? [[obj retain] autorelease] : nil;
}

id objc_storeWeak(id* location, id obj) {
  if (*location) [*location release];
  *location = obj ? [obj retain] : nil;
  return obj;
}
