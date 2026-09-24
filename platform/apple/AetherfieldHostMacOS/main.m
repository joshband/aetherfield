// macOS container app for AetherfieldAUExtensionMacOS.
// This app has no UI and is never intended to be used directly. It exists
// solely to embed and host the AUv3 extension via the system's extension
// host process. The minimal main() satisfies the requirement for the bundle
// to have a Mach-O executable; the extension itself is loaded out-of-process.

#import <Cocoa/Cocoa.h>

int main(int argc, char *argv[]) {
  return NSApplicationMain(argc, argv);
}
