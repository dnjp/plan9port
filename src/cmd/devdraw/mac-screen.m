#define Cursor OSXCursor
#define Point OSXPoint
#define Rect OSXRect

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#undef Cursor
#undef Point
#undef Rect

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>
#include <mouse.h>
#include <cursor.h>
#include <keyboard.h>
#include <drawfcall.h>
#include "devdraw.h"
#include "bigarrow.h"
#include "glendapng.h"

AUTOFRAMEWORK(Cocoa)
AUTOFRAMEWORK(Metal)
AUTOFRAMEWORK(QuartzCore)
AUTOFRAMEWORK(CoreFoundation)

#define LOG	if(1)NSLog

// ─────────────────────────────────────────────────────────────────────────────
// Child PID tracking — so applicationShouldTerminate can clean up 9term clients.
// ─────────────────────────────────────────────────────────────────────────────

#define MAXCHILDREN 64
static pid_t childpids[MAXCHILDREN];
static int   nchildpids = 0;

static void
addchildpid(pid_t pid)
{
	if(nchildpids < MAXCHILDREN)
		childpids[nchildpids++] = pid;
}

static void
removechildpid(pid_t pid)
{
	for(int i = 0; i < nchildpids; i++){
		if(childpids[i] == pid){
			childpids[i] = childpids[--nchildpids];
			return;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Unique client-id counter for wsysid= assignments.
// ─────────────────────────────────────────────────────────────────────────────

static int nextclientid = 0;

// ─────────────────────────────────────────────────────────────────────────────
// viewRegistry: maps raw pointer (uintptr_t) → CFRetain'd DrawView reference.
//
// We use CFTypeRef + explicit CFRetain/CFRelease instead of ARC strong
// references to avoid the autorelease-pool race that occurs when ARC returns
// ObjC objects from plain C functions across thread boundaries.
//
// Ownership rules:
//   viewRegistry_add   – CFRetain's the view (registry owns +1)
//   viewRegistry_get   – returns a +1 CFRetain'd reference; caller must
//                        CFRelease (or let ARC do it via __bridge_transfer)
//   viewRegistry_remove – CFRelease's the registry's +1 reference
//
// viewRegistry_remove must only be called from the main thread, after
// clientGone has been set, so that any queued flush: blocks bail out before
// the view's last reference is dropped.
// ─────────────────────────────────────────────────────────────────────────────

// viewRegistry: a small fixed array mapping DrawView pointer → CFRetain'd ref.
// We use CFTypeRef + explicit CFRetain/CFRelease instead of ARC/NSMutableDictionary
// to avoid autorelease-pool races when returning ObjC objects from C functions
// across thread boundaries.
//
// At most ~32 concurrent windows is realistic; 64 slots is plenty.
#define VREG_SIZE 64
typedef struct { uintptr_t key; CFTypeRef val; } VRegEntry;
static VRegEntry vregTable[VREG_SIZE];
static pthread_mutex_t viewRegistryLock = PTHREAD_MUTEX_INITIALIZER;

static void
viewRegistry_add(void *vp, id view)
{
	uintptr_t k = (uintptr_t)vp;
	if(k == 0 || view == nil)
		return;
	CFTypeRef ref = CFRetain((__bridge CFTypeRef)view);  // registry owns +1
	NSLog(@"viewRegistry_add: CFRetain'd %p retainCount=%lu", vp, (unsigned long)CFGetRetainCount(ref));
	pthread_mutex_lock(&viewRegistryLock);
	for(int i = 0; i < VREG_SIZE; i++){
		if(vregTable[i].key == 0){
			vregTable[i].key = k;
			vregTable[i].val = ref;
			pthread_mutex_unlock(&viewRegistryLock);
			return;
		}
	}
	pthread_mutex_unlock(&viewRegistryLock);
	CFRelease(ref);
	NSLog(@"viewRegistry_add: table full!");
}

// Returns a +1 CFRetain'd reference, or NULL.
// Use __bridge_transfer to hand the +1 to ARC, or CFRelease manually.
// Never dereferences vp.
static CFTypeRef
viewRegistry_getref(void *vp)
{
	uintptr_t k = (uintptr_t)vp;
	if(k == 0)
		return NULL;
	pthread_mutex_lock(&viewRegistryLock);
	for(int i = 0; i < VREG_SIZE; i++){
		if(vregTable[i].key == k){
			CFTypeRef ref = vregTable[i].val;
			if(ref)
				CFRetain(ref);  // +1 for caller
			pthread_mutex_unlock(&viewRegistryLock);
			return ref;
		}
	}
	pthread_mutex_unlock(&viewRegistryLock);
	return NULL;
}

// Must only be called from the main thread.
static void
viewRegistry_remove(void *vp)
{
	uintptr_t k = (uintptr_t)vp;
	if(k == 0)
		return;
	NSLog(@"viewRegistry_remove: removing %p", vp);
	CFTypeRef ref = NULL;
	pthread_mutex_lock(&viewRegistryLock);
	for(int i = 0; i < VREG_SIZE; i++){
		if(vregTable[i].key == k){
			ref = vregTable[i].val;
			vregTable[i].key = 0;
			vregTable[i].val = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&viewRegistryLock);
	NSLog(@"viewRegistry_remove: CFRelease for %p (found=%d)", vp, ref != NULL);
	if(ref)
		CFRelease(ref);  // drop registry's +1
	NSLog(@"viewRegistry_remove: done %p", vp);
}

// ─────────────────────────────────────────────────────────────────────────────

static void setprocname(const char*);
static uint keycvt(uint);
static uint msec(void);

static void	rpc_resizeimg(Client*);
static void	rpc_resizewindow(Client*, Rectangle);
static void	rpc_setcursor(Client*, Cursor*, Cursor2*);
static void	rpc_setlabel(Client*, char*);
static void	rpc_setmouse(Client*, Point);
static void	rpc_topwin(Client*);
static void	rpc_bouncemouse(Client*, Mouse);
static void	rpc_flush(Client*, Rectangle);

static ClientImpl macimpl = {
	rpc_resizeimg,
	rpc_resizewindow,
	rpc_setcursor,
	rpc_setlabel,
	rpc_setmouse,
	rpc_topwin,
	rpc_bouncemouse,
	rpc_flush
};

@class DrawView;
@class DrawLayer;

// Pid of the most recently spawned child, transferred to DrawView in initimg.
static pid_t pendingChildPid = 0;
// App name from CFBundleName (e.g. "9term", "acme", "sam"); set at launch.
static NSString *appName = @"devdraw";

@interface AppDelegate : NSObject<NSApplicationDelegate>
@end

static AppDelegate *myApp = NULL;

void
gfx_main(void)
{
	if(client0)
		setprocname(argv0);

	@autoreleasepool{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		myApp = [AppDelegate new];
		[NSApp setDelegate:myApp];
		[NSApp run];
	}
}

void
rpc_shutdown(void)
{
	[NSApp terminate:myApp];
}

// rpc_clientgone is defined after DrawView is fully declared (see below).

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(id)arg
{
	NSMenu *m, *sm;
	NSBundle *bundle;

	// Redirect stderr to a log file so we can see sysfatal/fprint output
	// when launched from the Dock (where stderr goes nowhere).
	freopen("/tmp/devdraw-bundle.log", "a", stderr);
	NSLog(@"[applicationDidFinishLaunching] srvname=%s argv0=%s",
	      srvname ? srvname : "(nil)", argv0 ? argv0 : "(nil)");

	bundle = [NSBundle mainBundle];
	NSString *bundleName = [bundle objectForInfoDictionaryKey:@"CFBundleName"];
	if(bundleName != nil && ![bundleName isEqualToString:@"devdraw"])
		appName = bundleName;

	sm = [NSMenu new];
	[sm addItemWithTitle:@"Toggle Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
	[sm addItemWithTitle:@"Hide" action:@selector(hide:) keyEquivalent:@"h"];
	[sm addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];

	m = [NSMenu new];
	[m addItemWithTitle:appName action:NULL keyEquivalent:@""];
	[m setSubmenu:sm forItem:[m itemAtIndex:0]];

	if(![appName isEqualToString:@"devdraw"]){
		NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
		NSMenuItem *newWin = [[NSMenuItem alloc]
		                      initWithTitle:@"New Window"
		                             action:@selector(newWindow:)
		                      keyEquivalent:@"n"];
		[newWin setTarget:self];
		[fileMenu addItem:newWin];
		if([appName isEqualToString:@"9term"]){
			NSMenuItem *newTab = [[NSMenuItem alloc]
			                      initWithTitle:@"New Tab"
			                             action:@selector(newTab:)
			                      keyEquivalent:@"t"];
			[newTab setTarget:self];
			[fileMenu addItem:newTab];
		}
		[fileMenu addItem:[NSMenuItem separatorItem]];
		NSMenuItem *closeWin = [[NSMenuItem alloc]
		                        initWithTitle:@"Close Window"
		                               action:@selector(closeWindow:)
		                        keyEquivalent:@"w"];
		[closeWin setTarget:self];
		[fileMenu addItem:closeWin];
		NSMenuItem *fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""];
		[fileMenuItem setSubmenu:fileMenu];
		[m addItem:fileMenuItem];
	}

	// Window menu — macOS populates it automatically when named "Window".
	NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
	NSMenuItem *windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:NULL keyEquivalent:@""];
	[windowMenuItem setSubmenu:windowMenu];
	[m addItem:windowMenuItem];
	[NSApp setWindowsMenu:windowMenu];

	[NSApp setMainMenu:m];

	if([bundle objectForInfoDictionaryKey:@"CFBundleIconFile"] == nil){
		NSData *d = [[NSData alloc] initWithBytes:glenda_png length:(sizeof glenda_png)];
		NSImage *i = [[NSImage alloc] initWithData:d];
		[NSApp setApplicationIconImage:i];
		[[NSApp dockTile] display];
	}

	NSLog(@"[applicationDidFinishLaunching] calling gfx_started");
	gfx_started();
	NSLog(@"[applicationDidFinishLaunching] gfx_started returned");

	// In server mode, spawn the first client after the run loop has started
	// so that listenproc is actually running and ready to accept connections.
	if(srvname != nil){
		NSLog(@"[applicationDidFinishLaunching] queuing newWindow");
		AppDelegate *delegate = self;
		dispatch_async(dispatch_get_main_queue(), ^{
			NSLog(@"[applicationDidFinishLaunching] dispatched newWindow firing");
			[delegate newWindow:nil];
		});
	}
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
	// Only quit when all clients are gone, not just when a window closes
	// (closing a tab closes its window but other clients may still be running).
	return nclients == 0;
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
	return YES;
}

- (void)closeWindow:(id)sender
{
	[[NSApp keyWindow] performClose:sender];
}

// Dock icon clicked while running with no visible windows — open a new one.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)hasVisibleWindows
{
	if(!hasVisibleWindows && srvname != nil)
		[self newWindow:nil];
	return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
	// Kill all tracked child 9term processes.
	for(int i = 0; i < nchildpids; i++)
		kill(childpids[i], SIGTERM);
	return NSTerminateNow;
}

// New Window: spawn a 9term client that connects to our devdraw server via wsysid.
- (void)newWindow:(id)sender
{
	if(srvname == nil){
		// Legacy mode: no server, nothing to connect to.
		fprint(2, "devdraw: newWindow: not running in server mode\n");
		return;
	}

	// Find the 9term binary: it lives next to devdraw in the bundle.
	NSString *bindir = [[[NSBundle mainBundle] executablePath]
	                     stringByDeletingLastPathComponent];
	// The client binary name matches the bundle name (e.g. "9term", "acme").
	NSString *clientname = [[NSBundle mainBundle]
	                         objectForInfoDictionaryKey:@"CFBundleName"];
	if(clientname == nil) clientname = @"9term";
	// The actual 9term binary lives alongside devdraw; strip the launcher name.
	// Prefer $PLAN9 if set (terminal launch), otherwise use the default install path.
	const char *plan9env = getenv("PLAN9");
	NSString *plan9 = plan9env
		? [NSString stringWithUTF8String:plan9env]
		: @"/usr/local/plan9";
	NSString *clientbin = [[plan9 stringByAppendingPathComponent:@"bin"]
	                            stringByAppendingPathComponent:clientname];

	// Assign a unique wsysid for this client.
	int cid = nextclientid++;
	NSString *wsysid = [NSString stringWithFormat:@"%s/%d",
	                    srvname, cid];

	NSDictionary *curEnv = [[NSProcessInfo processInfo] environment];
	NSMutableDictionary *childEnv = [curEnv mutableCopy];
	childEnv[@"wsysid"] = wsysid;
	childEnv[@"INSIDE_P9P"] = @"true";
	childEnv[@"INSIDE_9TERM"] = @"true";
	// Ensure DEVDRAW points to our bundle's devdraw so the client finds us.
	childEnv[@"DEVDRAW"] = [bindir stringByAppendingPathComponent:@"devdraw"];
	// Ensure PLAN9 and PATH are set for clients launched from the Dock.
	childEnv[@"PLAN9"] = plan9;
	NSString *plan9bin = [plan9 stringByAppendingPathComponent:@"bin"];
	NSString *curPath = childEnv[@"PATH"] ?: @"/usr/bin:/bin";
	if([curPath rangeOfString:plan9bin].location == NSNotFound)
		childEnv[@"PATH"] = [NSString stringWithFormat:@"%@:%@", plan9bin, curPath];

	NSArray *keys = [childEnv allKeys];
	char **envp = malloc(([keys count] + 1) * sizeof(char*));
	if(envp == nil){ fprint(2, "devdraw: newWindow: malloc\n"); return; }
	for(NSUInteger i = 0; i < [keys count]; i++){
		NSString *k = keys[i];
		envp[i] = strdup([[NSString stringWithFormat:@"%@=%@", k, childEnv[k]] UTF8String]);
	}
	envp[[keys count]] = nil;

	const char *path = [clientbin UTF8String];
	char *argv[] = { (char*)path, nil };
	pid_t pid;
	int err = posix_spawn(&pid, path, nil, nil, argv, envp);
	NSLog(@"[newWindow] spawn %s wsysid=%@ err=%d", path, wsysid, err);
	if(err != 0)
		NSLog(@"[newWindow] posix_spawn failed: %s", strerror(err));
	else {
		addchildpid(pid);
		pendingChildPid = pid;
	}

	for(NSUInteger i = 0; i < [keys count]; i++) free(envp[i]);
	free(envp);
}

- (void)newTab:(id)sender
{
	// Signal topwin to merge the next new window as a tab by setting
	// Preferred tabbing mode on the current key window.
	NSWindow *keyWin = [NSApp keyWindow];
	if(keyWin)
		keyWin.tabbingMode = NSWindowTabbingModePreferred;
	[self newWindow:sender];
}

- (void)bringWindowToFront:(NSMenuItem*)item
{
	NSWindow *win = [item representedObject];
	[win makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}
@end

@interface DrawLayer : CAMetalLayer
@property (nonatomic, retain) id<MTLCommandQueue> cmd;
@property (nonatomic, retain) id<MTLTexture> texture;
@end

@implementation DrawLayer
- (void)display
{
	LOG(@"display");
	LOG(@"display query drawable");

	@autoreleasepool{
		id<CAMetalDrawable> drawable = [self nextDrawable];
		if(!drawable){
			LOG(@"display couldn't get drawable");
			[self setNeedsDisplay];
			return;
		}

		LOG(@"display got drawable");

		id<MTLCommandBuffer> cbuf = [self.cmd commandBuffer];
		id<MTLBlitCommandEncoder> blit = [cbuf blitCommandEncoder];
		[blit copyFromTexture:self.texture
			sourceSlice:0
			sourceLevel:0
			sourceOrigin:MTLOriginMake(0, 0, 0)
			sourceSize:MTLSizeMake(self.texture.width, self.texture.height, self.texture.depth)
			toTexture:drawable.texture
			destinationSlice:0
			destinationLevel:0
			destinationOrigin:MTLOriginMake(0, 0, 0)];
		[blit endEncoding];

		[cbuf presentDrawable:drawable];
		drawable = nil;
		[cbuf addCompletedHandler:^(id<MTLCommandBuffer> cmdBuff){
			if(cmdBuff.error){
				NSLog(@"command buffer finished with error: %@",
					cmdBuff.error.localizedDescription);
			}else
				LOG(@"command buffer finishes present drawable");
		}];
		[cbuf commit];
	}
	LOG(@"display commit");
}
@end

@interface DrawView : NSView<NSTextInputClient,NSWindowDelegate>
@property (nonatomic, assign) Client *client;
@property (nonatomic, retain) DrawLayer *dlayer;
@property (nonatomic, retain) NSWindow *win;
@property (nonatomic, retain) NSCursor *currentCursor;
@property (nonatomic, assign) Memimage *img;
@property (nonatomic, assign) BOOL clientGone;
@property (nonatomic, assign) pid_t childPid;

- (id)attach:(Client*)client winsize:(char*)winsize label:(char*)label;
- (void)topwin;
- (void)setlabel:(char*)label;
- (void)setcursor:(Cursor*)c cursor2:(Cursor2*)c2;
- (void)setmouse:(Point)p;
- (void)clearInput;
- (void)getmouse:(NSEvent*)e;
- (void)sendmouse:(NSUInteger)b;
- (void)resetLastInputRect;
- (void)enlargeLastInputRect:(NSRect)r;
@end

@implementation DrawView
{
	NSMutableString *_tmpText;
	NSRange _markedRange;
	NSRange _selectedRange;
	NSRect _lastInputRect;	// The view is flipped, this is not.
	BOOL _tapping;
	NSUInteger _tapFingers;
	NSUInteger _tapTime;
}

- (id)init
{
	LOG(@"View init");
	self = [super init];
	[self setAllowedTouchTypes:NSTouchTypeMaskDirect|NSTouchTypeMaskIndirect];
	_tmpText = [[NSMutableString alloc] initWithCapacity:2];
	_markedRange = NSMakeRange(NSNotFound, 0);
	_selectedRange = NSMakeRange(0, 0);
	return self;
}

- (CALayer*)makeBackingLayer { return [DrawLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

// rpc_attach allocates a new screen window with the given label and size
// and attaches it to client c (by setting c->view).
Memimage*
rpc_attach(Client *c, char *label, char *winsize)
{
	LOG(@"attachscreen(%s, %s)", label, winsize);

	c->impl = &macimpl;
	dispatch_sync(dispatch_get_main_queue(), ^(void) {
		@autoreleasepool {
			DrawView *view = [[DrawView new] attach:c winsize:winsize label:label];
			[view initimg];
		}
	});
	return ((__bridge DrawView*)c->view).img;
}

- (id)attach:(Client*)client winsize:(char*)winsize label:(char*)label {
	NSRect r, sr;
	Rectangle wr;
	int set;
	char *s;
	NSArray *allDevices;

	NSWindowStyleMask Winstyle = NSWindowStyleMaskTitled
		| NSWindowStyleMaskClosable
		| NSWindowStyleMaskMiniaturizable
		| NSWindowStyleMaskResizable;

	if(label == nil || *label == '\0')
		Winstyle &= ~NSWindowStyleMaskTitled;

	s = winsize;
	sr = [[NSScreen mainScreen] frame];
	r = [[NSScreen mainScreen] visibleFrame];

	LOG(@"makewin(%s)", s);
	if(s == nil || *s == '\0' || parsewinsize(s, &wr, &set) < 0) {
		wr = Rect(0, 0, sr.size.width*2/3, sr.size.height*2/3);
		set = 0;
	}

	r.origin.x = wr.min.x;
	r.origin.y = sr.size.height-wr.max.y;	/* winsize is top-left-based */
	r.size.width = fmin(Dx(wr), r.size.width);
	r.size.height = fmin(Dy(wr), r.size.height);
	r = [NSWindow contentRectForFrameRect:r styleMask:Winstyle];

	NSWindow *win = [[NSWindow alloc]
		initWithContentRect:r
		styleMask:Winstyle
		backing:NSBackingStoreBuffered defer:NO];
	[win setTitle:appName];

	if(!set)
		[win center];
	// NSWindowCollectionBehaviorManaged opts the window into the tab bar.
	// Only 9term supports tabs; acme and sam use separate windows only.
	NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorFullScreenPrimary;
	if([appName isEqualToString:@"9term"])
		behavior |= NSWindowCollectionBehaviorManaged;
	[win setCollectionBehavior:behavior];
	[win setContentMinSize:NSMakeSize(64,64)];
	[win setOpaque:YES];
	[win setRestorable:NO];
	[win setAcceptsMouseMovedEvents:YES];

	client->view = (__bridge void*)self;
	NSLog(@"initimg: registering view %p pid=%d", client->view, pendingChildPid);
	self.childPid = pendingChildPid;
	pendingChildPid = 0;
	viewRegistry_add(client->view, self);
	self.client = client;
	self.win = win;
	self.currentCursor = nil;
	[win setContentView:self];
	[win setDelegate:self];
	// Use a notification observer in addition to the delegate method because
	// addTabbedWindow: can replace the window's delegate, causing
	// windowWillClose: to never fire.
	NSLog(@"attach: registering NSWindowWillCloseNotification observer for view=%p win=%p", (__bridge void*)self, (__bridge void*)win);
	[[NSNotificationCenter defaultCenter]
		addObserver:self
		selector:@selector(windowWillClose:)
		name:NSWindowWillCloseNotification
		object:win];
	[self setWantsLayer:YES];
	[self setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawOnSetNeedsDisplay];

	id<MTLDevice> device = nil;
	allDevices = MTLCopyAllDevices();
	for(id mtlDevice in allDevices) {
		if ([mtlDevice isLowPower] && ![mtlDevice isRemovable]) {
			device = mtlDevice;
			break;
		}
	}
	if(!device)
		device = MTLCreateSystemDefaultDevice();

	DrawLayer *layer = (DrawLayer*)[self layer];
	self.dlayer = layer;
	layer.device = device;
	layer.cmd = [device newCommandQueue];
	layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	layer.framebufferOnly = YES;
	layer.opaque = YES;

	// We use a default transparent layer on top of the CAMetalLayer.
	// This seems to make fullscreen applications behave.
	// Specifically, without this code if you enter full screen with Cmd-F,
	// the screen goes black until the first mouse click.
	if(1) {
		CALayer *stub = [CALayer layer];
		stub.frame = CGRectMake(0, 0, 1, 1);
		[stub setNeedsDisplay];
		[layer addSublayer:stub];
	}

	[NSEvent setMouseCoalescingEnabled:NO];

	[self topwin];
	[self setlabel:label];
	[self setcursor:nil cursor2:nil];

	return self;
}

// rpc_topwin moves the window to the top of the desktop.
// Called from an RPC thread with no client lock held.
static void
rpc_topwin(Client *c)
{
	DrawView *view = (__bridge DrawView*)c->view;
	dispatch_sync(dispatch_get_main_queue(), ^(void) {
		[view topwin];
	});
}

- (void)topwin {
	// If the user asked for a new tab (tabbingMode == Preferred on the key
	// window), merge this window into that tab group instead of opening a
	// separate window.
	NSWindow *keyWin = [NSApp keyWindow];
	if(keyWin && keyWin != self.win
	   && keyWin.tabbingMode == NSWindowTabbingModePreferred) {
		[keyWin addTabbedWindow:self.win ordered:NSWindowAbove];
		keyWin.tabbingMode = NSWindowTabbingModeAutomatic;
	}
	[self.win makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}

// rpc_setlabel updates the client window's label.
// If label == nil, the call is a no-op.
// Called from an RPC thread with no client lock held.
static void
rpc_setlabel(Client *client, char *label)
{
	DrawView *view = (__bridge DrawView*)client->view;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		[view setlabel:label];
	});
}

- (void)setlabel:(char*)label {
	LOG(@"setlabel(%s)", label);
	if(label == nil)
		return;

	@autoreleasepool{
		// Always show the app name (9term, acme, sam) in the title bar.
		// appName is set from CFBundleName at launch; fall back to the
		// client-supplied label only when running outside a bundle.
		[self.win setTitle:appName];
	}
}

// rpc_setcursor updates the client window's cursor image.
// Either c and c2 are both non-nil, or they are both nil to use the default arrow.
// Called from an RPC thread with no client lock held.
static void
rpc_setcursor(Client *client, Cursor *c, Cursor2 *c2)
{
	DrawView *view = (__bridge DrawView*)client->view;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		[view setcursor:c cursor2:c2];
	});
}

- (void)setcursor:(Cursor*)c cursor2:(Cursor2*)c2 {
	if(!c) {
		c = &bigarrow;
		c2 = &bigarrow2;
	}

	NSBitmapImageRep *r, *r2;
	NSImage *i;
	NSPoint p;
	uchar *plane[5], *plane2[5];
	uint b;

	r = [[NSBitmapImageRep alloc]
		initWithBitmapDataPlanes:nil
		pixelsWide:16
		pixelsHigh:16
		bitsPerSample:1
		samplesPerPixel:2
		hasAlpha:YES
		isPlanar:YES
		colorSpaceName:NSDeviceWhiteColorSpace
		bytesPerRow:2
		bitsPerPixel:0];
	[r getBitmapDataPlanes:plane];
	for(b=0; b<nelem(c->set); b++){
		plane[0][b] = ~c->set[b] & c->clr[b];
		plane[1][b] = c->set[b] | c->clr[b];
	}

	r2 = [[NSBitmapImageRep alloc]
		initWithBitmapDataPlanes:nil
		pixelsWide:32
		pixelsHigh:32
		bitsPerSample:1
		samplesPerPixel:2
		hasAlpha:YES
		isPlanar:YES
		colorSpaceName:NSDeviceWhiteColorSpace
		bytesPerRow:4
		bitsPerPixel:0];
	[r2 getBitmapDataPlanes:plane2];
	for(b=0; b<nelem(c2->set); b++){
		plane2[0][b] = ~c2->set[b] & c2->clr[b];
		plane2[1][b] = c2->set[b] | c2->clr[b];
	}

	static BOOL debug = NO;
	if(debug){
		NSData *data = [r representationUsingType: NSBitmapImageFileTypeBMP properties: @{}];
		[data writeToFile: @"/tmp/r.bmp" atomically: NO];
		data = [r2 representationUsingType: NSBitmapImageFileTypeBMP properties: @{}];
		[data writeToFile: @"/tmp/r2.bmp" atomically: NO];
		debug = NO;
	}

	i = [[NSImage alloc] initWithSize:NSMakeSize(16, 16)];
	[i addRepresentation:r2];
	[i addRepresentation:r];

	p = NSMakePoint(-c->offset.x, -c->offset.y);
	self.currentCursor = [[NSCursor alloc] initWithImage:i hotSpot:p];
	[self.win invalidateCursorRectsForView:self];
}

- (void)initimg {
	@autoreleasepool {
		CGFloat scale;
		NSSize size;
		MTLTextureDescriptor *textureDesc;

		size = [self convertSizeToBacking:[self bounds].size];
		self.client->mouserect = Rect(0, 0, size.width, size.height);

		LOG(@"initimg %.0f %.0f", size.width, size.height);

		self.img = allocmemimage(self.client->mouserect, XRGB32);
		if(self.img == nil)
			panic("allocmemimage: %r");
		if(self.img->data == nil)
			panic("img->data == nil");

		textureDesc = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
			width:size.width
			height:size.height
			mipmapped:NO];
		textureDesc.allowGPUOptimizedContents = YES;
		textureDesc.usage = MTLTextureUsageShaderRead;
		textureDesc.cpuCacheMode = MTLCPUCacheModeWriteCombined;
		self.dlayer.texture = [self.dlayer.device newTextureWithDescriptor:textureDesc];

		scale = [self.win backingScaleFactor];
		[self.dlayer setDrawableSize:size];
		[self.dlayer setContentsScale:scale];

		// NOTE: This is not really the display DPI.
		// On retina, scale is 2; otherwise it is 1.
		// This formula gives us 220 for retina, 110 otherwise.
		// That's not quite right but it's close to correct.
		// https://en.wikipedia.org/wiki/Retina_display#Models
		self.client->displaydpi = scale * 110;
	}
}

// rpc_flush flushes changes to view.img's rectangle r
// to the on-screen window, making them visible.
// Called from an RPC thread with no client lock held.
static void
rpc_flush(Client *client, Rectangle r)
{
	// viewRegistry_getref returns a +1 CFRetain'd reference.
	// Transfer it into a strong ARC variable so the block captures it
	// with a proper retain, and ARC releases it when the block completes.
	CFTypeRef ref = viewRegistry_getref(client->view);
	if(ref == nil)
		return;
	DrawView *view = (__bridge_transfer DrawView*)ref;  // ARC now owns the +1
	NSLog(@"rpc_flush: dispatching flush for view %p (clientGone=%d)", client->view, (int)view.clientGone);
	dispatch_async(dispatch_get_main_queue(), ^(void){
		NSLog(@"rpc_flush block: running flush for view %p (clientGone=%d)", (__bridge void*)view, (int)view.clientGone);
		[view flush:r];
	});
}

- (void)dealloc {
	NSLog(@"DrawView dealloc: %p", (__bridge void*)self);
	[[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)flush:(Rectangle)r {
	@autoreleasepool{
		NSLog(@"flush: enter self=%p clientGone=%d", (__bridge void*)self, (int)self.clientGone);
		if(self.clientGone || self.img == nil || self.win == nil)
			return;
		if(!rectclip(&r, Rect(0, 0, self.dlayer.texture.width, self.dlayer.texture.height)) || !rectclip(&r, self.img->r))
			return;

		// drawlk protects the pixel data in self.img.
		// In addition to avoiding a technical data race,
		// the lock avoids drawing partial updates, which makes
		// animations like sweeping windows much less flickery.
		qlock(&drawlk);
		[self.dlayer.texture
			replaceRegion:MTLRegionMake2D(r.min.x, r.min.y, Dx(r), Dy(r))
			mipmapLevel:0
			withBytes:byteaddr(self.img, Pt(r.min.x, r.min.y))
			bytesPerRow:self.img->width*sizeof(u32int)];
		qunlock(&drawlk);

		NSRect nr = NSMakeRect(r.min.x, r.min.y, Dx(r), Dy(r));
		dispatch_time_t time;

		LOG(@"callsetNeedsDisplayInRect(%g, %g, %g, %g)", nr.origin.x, nr.origin.y, nr.size.width, nr.size.height);
		nr = [self.win convertRectFromBacking:nr];
		LOG(@"setNeedsDisplayInRect(%g, %g, %g, %g)", nr.origin.x, nr.origin.y, nr.size.width, nr.size.height);
		[self.dlayer setNeedsDisplayInRect:nr];

		// Schedule a second display pass 16ms later to catch any missed frames.
		DrawView *__weak weakSelf = self;
		time = dispatch_time(DISPATCH_TIME_NOW, 16 * NSEC_PER_MSEC);
		dispatch_after(time, dispatch_get_main_queue(), ^(void){
			DrawView *s = weakSelf;
			if(s && !s.clientGone)
				[s.dlayer setNeedsDisplayInRect:nr];
		});

		[self enlargeLastInputRect:nr];
	}
}

// rpc_resizeimg forces the client window to discard its current window and make a new one.
// It is called when the user types Cmd-R to toggle whether retina mode is forced.
// Called from an RPC thread with no client lock held.
static void
rpc_resizeimg(Client *c)
{
	DrawView *view = (__bridge DrawView*)c->view;
	dispatch_async(dispatch_get_main_queue(), ^(void){
		[view resizeimg];
	});
}

- (void)resizeimg {
	[self initimg];
	gfx_replacescreenimage(self.client, self.img);
}

// rpc_clientgone is called by serveproc when a server-mode client disconnects.
// Closes the client's NSWindow and frees the Client struct on the main thread.
void
rpc_clientgone(Client *c)
{
	// Capture the view pointer now (background thread) so we can pass it to
	// the main-thread block without touching c after free(c).
	void *vp = c->view;

	dispatch_async(dispatch_get_main_queue(), ^{
		if(vp){
			// Obtain a +1 retained reference while the registry still holds one.
			CFTypeRef ref = viewRegistry_getref(vp);
			DrawView *view = (__bridge_transfer DrawView*)ref;  // ARC owns +1
			NSLog(@"rpc_clientgone: main block running, vp=%p view=%p", vp, (__bridge void*)view);
			if(view && !view.clientGone){
				// Client disconnected without the window being closed (e.g. crash).
				// Mark gone, hide the window, and remove the notification observer.
				view.clientGone = YES;
				view.img = nil;
				[[NSNotificationCenter defaultCenter] removeObserver:view
					name:NSWindowWillCloseNotification
					object:view.win];
				[view.win orderOut:nil];
				[view.win setDelegate:nil];
			}
			// Remove from registry — drops the registry's CFRetain.
			// By the time this dispatch_async runs, all autorelease pools from
			// the window-close event have drained, so this is safe.
			NSLog(@"rpc_clientgone: removing from registry vp=%p", vp);
			viewRegistry_remove(vp);
			NSLog(@"rpc_clientgone: removed from registry vp=%p", vp);
		}
		free(c);
	});
}

- (void)windowWillClose:(NSNotification *)notification {
	NSLog(@"windowWillClose: view=%p pid=%d clientGone=%d", (__bridge void*)self, (int)self.childPid, (int)self.clientGone);
	// Remove the notification observer so we don't get called twice
	// (once as delegate, once as notification observer).
	[[NSNotificationCenter defaultCenter] removeObserver:self
		name:NSWindowWillCloseNotification
		object:self.win];
	if(self.clientGone)
		return;

	// Mark gone immediately so any queued flush: blocks bail out.
	self.clientGone = YES;
	self.img = nil;

	// DO NOT call viewRegistry_remove here.  NSWindow autoreleases its
	// content view during close, leaving a dangling pool entry.  If we
	// drop the registry's CFRetain now, the refcount can hit 0 and dealloc
	// fires immediately; then when the pool drains it crashes on the freed
	// object.  Instead, keep the registry's CFRetain alive and let
	// rpc_clientgone (triggered by the child dying) call viewRegistry_remove
	// on a future run-loop turn, after all pools have drained.
	//
	// Kill the child so serveproc exits and rpc_clientgone fires.
	pid_t pid = self.childPid;
	if(pid > 0){
		NSLog(@"windowWillClose: killing pid %d", (int)pid);
		kill(pid, SIGTERM);
		removechildpid(pid);
		self.childPid = 0;
	}
}

- (void)windowDidResize:(NSNotification *)notification {
	if(![self inLiveResize] && self.img) {
		[self resizeimg];
	}
}
- (void)viewDidEndLiveResize
{
	[super viewDidEndLiveResize];
	if(self.img)
		[self resizeimg];
}

- (void)viewDidChangeBackingProperties
{
	[super viewDidChangeBackingProperties];
	if(self.img)
		[self resizeimg];
}

- (void)setFrameSize:(NSSize)newSize
{
	[super setFrameSize:newSize];
	if(self.img)
		[self resizeimg];
}

// rpc_resizewindow asks for the client window to be resized to size r.
// Called from an RPC thread with no client lock held.
static void
rpc_resizewindow(Client *c, Rectangle r)
{
	DrawView *view = (__bridge DrawView*)c->view;

	LOG(@"resizewindow %d %d %d %d", r.min.x, r.min.y, Dx(r), Dy(r));
	dispatch_async(dispatch_get_main_queue(), ^(void){
		NSSize s;

		s = [view convertSizeFromBacking:NSMakeSize(Dx(r), Dy(r))];
		[view.win setContentSize:s];
	});
}


- (void)windowDidBecomeKey:(id)arg {
	[self sendmouse:0];
}

- (void)windowDidResignKey:(id)arg {
	gfx_abortcompose(self.client);
}

- (void)mouseMoved:(NSEvent*)e{ [self getmouse:e];}
- (void)mouseDown:(NSEvent*)e{ [self getmouse:e];}
- (void)mouseDragged:(NSEvent*)e{ [self getmouse:e];}
- (void)mouseUp:(NSEvent*)e{ [self getmouse:e];}
- (void)otherMouseDown:(NSEvent*)e{ [self getmouse:e];}
- (void)otherMouseDragged:(NSEvent*)e{ [self getmouse:e];}
- (void)otherMouseUp:(NSEvent*)e{ [self getmouse:e];}
- (void)rightMouseDown:(NSEvent*)e{ [self getmouse:e];}
- (void)rightMouseDragged:(NSEvent*)e{ [self getmouse:e];}
- (void)rightMouseUp:(NSEvent*)e{ [self getmouse:e];}

- (void)scrollWheel:(NSEvent*)e
{
	CGFloat s;

	s = [e scrollingDeltaY];
	if(s > 0.0f)
		[self sendmouse:8];
	else if (s < 0.0f)
		[self sendmouse:16];
}

// Intercept Cmd+W (close window) and Cmd+Q (quit app) before they are
// forwarded to the Plan 9 key handler as Kcmd+'w'/'q' keystrokes.
- (BOOL)performKeyEquivalent:(NSEvent*)e
{
	if([e modifierFlags] & NSEventModifierFlagCommand){
		NSString *chars = [e charactersIgnoringModifiers];
		if([chars isEqualToString:@"w"]){
			[NSApp sendAction:@selector(closeWindow:) to:nil from:self];
			return YES;
		}
		if([chars isEqualToString:@"q"]){
			[NSApp terminate:nil];
			return YES;
		}
	}
	return [super performKeyEquivalent:e];
}

- (void)keyDown:(NSEvent*)e
{
	LOG(@"keyDown to interpret");

	[self interpretKeyEvents:[NSArray arrayWithObject:e]];

	[self resetLastInputRect];
}

- (void)flagsChanged:(NSEvent*)e
{
	static NSEventModifierFlags omod;
	NSEventModifierFlags m;
	uint b;

	LOG(@"flagsChanged");
	m = [e modifierFlags];

	b = [NSEvent pressedMouseButtons];
	b = (b&~6) | (b&4)>>1 | (b&2)<<1;
	if(b){
		int x;
		x = 0;
		if(m & ~omod & NSEventModifierFlagControl)
			x = 1;
		if(m & ~omod & NSEventModifierFlagOption)
			x = 2;
		if(m & ~omod & NSEventModifierFlagCommand)
			x = 4;
		b |= x;
		if(m & NSEventModifierFlagShift)
			b <<= 5;
		[self sendmouse:b];
	}else if(m & ~omod & NSEventModifierFlagOption)
		gfx_keystroke(self.client, Kalt);

	omod = m;
}

- (void)magnifyWithEvent:(NSEvent*)e
{
	if(fabs([e magnification]) > 0.02)
		[[self window] toggleFullScreen:nil];
}

- (void)touchesBeganWithEvent:(NSEvent*)e
{
	_tapping = YES;
	_tapFingers = [e touchesMatchingPhase:NSTouchPhaseTouching inView:nil].count;
	_tapTime = msec();
}
- (void)touchesMovedWithEvent:(NSEvent*)e
{
	_tapping = NO;
}
- (void)touchesEndedWithEvent:(NSEvent*)e
{
	if(_tapping
		&& [e touchesMatchingPhase:NSTouchPhaseTouching inView:nil].count == 0
		&& msec() - _tapTime < 250){
		switch(_tapFingers){
		case 3:
			[self sendmouse:2];
			[self sendmouse:0];
			break;
		case 4:
			[self sendmouse:2];
			[self sendmouse:1];
			[self sendmouse:0];
			break;
		}
		_tapping = NO;
	}
}
- (void)touchesCancelledWithEvent:(NSEvent*)e
{
	_tapping = NO;
}

- (void)getmouse:(NSEvent *)e
{
	NSUInteger b;
	NSEventModifierFlags m;

	b = [NSEvent pressedMouseButtons];
	b = b&~6 | (b&4)>>1 | (b&2)<<1;
	b = mouseswap(b);

	m = [e modifierFlags];
	if(b == 1){
		if(m & NSEventModifierFlagOption){
			gfx_abortcompose(self.client);
			b = 2;
		}else
		if(m & NSEventModifierFlagCommand)
			b = 4;
	}
	if(m & NSEventModifierFlagShift)
		b <<= 5;
	[self sendmouse:b];
}

- (void)sendmouse:(NSUInteger)b
{
	NSPoint p;

	p = [self.window convertPointToBacking:
		[self.window mouseLocationOutsideOfEventStream]];
	p.y = Dy(self.client->mouserect) - p.y;
	// LOG(@"(%g, %g) <- sendmouse(%d)", p.x, p.y, (uint)b);
	gfx_mousetrack(self.client, p.x, p.y, b, msec());
	if(b && _lastInputRect.size.width && _lastInputRect.size.height)
		[self resetLastInputRect];
}

// rpc_setmouse moves the mouse cursor.
// Called from an RPC thread with no client lock held.
static void
rpc_setmouse(Client *c, Point p)
{
	DrawView *view = (__bridge DrawView*)c->view;
	dispatch_async(dispatch_get_main_queue(), ^(void){
		[view setmouse:p];
	});
}

- (void)setmouse:(Point)p {
	@autoreleasepool{
		NSPoint q;

		LOG(@"setmouse(%d,%d)", p.x, p.y);
		q = [self.win convertPointFromBacking:NSMakePoint(p.x, p.y)];
		LOG(@"(%g, %g) <- fromBacking", q.x, q.y);
		q = [self convertPoint:q toView:nil];
		LOG(@"(%g, %g) <- toWindow", q.x, q.y);
		q = [self.win convertPointToScreen:q];
		LOG(@"(%g, %g) <- toScreen", q.x, q.y);
		// Quartz has the origin of the "global display
		// coordinate space" at the top left of the primary
		// screen with y increasing downward, while Cocoa has
		// the origin at the bottom left of the primary screen
		// with y increasing upward.  We flip the coordinate
		// with a negative sign and shift upward by the height
		// of the primary screen.
		q.y = NSScreen.screens[0].frame.size.height - q.y;
		LOG(@"(%g, %g) <- setmouse", q.x, q.y);
		CGWarpMouseCursorPosition(NSPointToCGPoint(q));
		CGAssociateMouseAndMouseCursorPosition(true);
	}
}


- (void)resetCursorRects {
	[super resetCursorRects];
	[self addCursorRect:self.bounds cursor:self.currentCursor];
}

// conforms to protocol NSTextInputClient
- (BOOL)hasMarkedText { return _markedRange.location != NSNotFound; }
- (NSRange)markedRange { return _markedRange; }
- (NSRange)selectedRange { return _selectedRange; }

- (void)setMarkedText:(id)string
	selectedRange:(NSRange)sRange
	replacementRange:(NSRange)rRange
{
	NSString *str;

	LOG(@"setMarkedText: %@ (%ld, %ld) (%ld, %ld)", string,
		sRange.location, sRange.length,
		rRange.location, rRange.length);

	[self clearInput];

	if([string isKindOfClass:[NSAttributedString class]])
		str = [string string];
	else
		str = string;

	if(rRange.location == NSNotFound){
		if(_markedRange.location != NSNotFound){
			rRange = _markedRange;
		}else{
			rRange = _selectedRange;
		}
	}

	if(str.length == 0){
		[_tmpText deleteCharactersInRange:rRange];
		[self unmarkText];
	}else{
		_markedRange = NSMakeRange(rRange.location, str.length);
		[_tmpText replaceCharactersInRange:rRange withString:str];
	}
	_selectedRange.location = rRange.location + sRange.location;
	_selectedRange.length = sRange.length;

	if(_tmpText.length){
		uint i;
		LOG(@"text length %ld", _tmpText.length);
		for(i = 0; i <= _tmpText.length; ++i){
			if(i == _markedRange.location)
				gfx_keystroke(self.client, '[');
			if(_selectedRange.length){
				if(i == _selectedRange.location)
					gfx_keystroke(self.client, '{');
				if(i == NSMaxRange(_selectedRange))
					gfx_keystroke(self.client, '}');
				}
			if(i == NSMaxRange(_markedRange))
				gfx_keystroke(self.client, ']');
			if(i < _tmpText.length)
				gfx_keystroke(self.client, [_tmpText characterAtIndex:i]);
		}
		int l;
		l = 1 + _tmpText.length - NSMaxRange(_selectedRange)
			+ (_selectedRange.length > 0);
		LOG(@"move left %d", l);
		for(i = 0; i < l; ++i)
			gfx_keystroke(self.client, Kleft);
	}

	LOG(@"text: \"%@\"  (%ld,%ld)  (%ld,%ld)", _tmpText,
		_markedRange.location, _markedRange.length,
		_selectedRange.location, _selectedRange.length);
}

- (void)unmarkText {
	//NSUInteger i;
	NSUInteger len;

	LOG(@"unmarkText");
	len = [_tmpText length];
	//for(i = 0; i < len; ++i)
	//	gfx_keystroke(self.client, [_tmpText characterAtIndex:i]);
	[_tmpText deleteCharactersInRange:NSMakeRange(0, len)];
	_markedRange = NSMakeRange(NSNotFound, 0);
	_selectedRange = NSMakeRange(0, 0);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
	LOG(@"validAttributesForMarkedText");
	return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)r
	actualRange:(NSRangePointer)actualRange
{
	NSRange sr;
	NSAttributedString *s;

	LOG(@"attributedSubstringForProposedRange: (%ld, %ld) (%ld, %ld)",
		r.location, r.length, actualRange->location, actualRange->length);
	sr = NSMakeRange(0, [_tmpText length]);
	sr = NSIntersectionRange(sr, r);
	if(actualRange)
		*actualRange = sr;
	LOG(@"use range: %ld, %ld", sr.location, sr.length);
	s = nil;
	if(sr.length)
		s = [[NSAttributedString alloc]
			initWithString:[_tmpText substringWithRange:sr]];
	LOG(@"	return %@", s);
	return s;
}

- (void)insertText:(id)s replacementRange:(NSRange)r {
	NSUInteger i;
	NSUInteger len;

	LOG(@"insertText: %@ replacementRange: %ld, %ld", s, r.location, r.length);

	[self clearInput];

	len = [s length];
	for(i = 0; i < len; ++i)
		gfx_keystroke(self.client, [s characterAtIndex:i]);
	[_tmpText deleteCharactersInRange:NSMakeRange(0, _tmpText.length)];
	_markedRange = NSMakeRange(NSNotFound, 0);
	_selectedRange = NSMakeRange(0, 0);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
	LOG(@"characterIndexForPoint: %g, %g", point.x, point.y);
	return 0;
}

- (NSRect)firstRectForCharacterRange:(NSRange)r actualRange:(NSRangePointer)actualRange {
	LOG(@"firstRectForCharacterRange: (%ld, %ld) (%ld, %ld)",
		r.location, r.length, actualRange->location, actualRange->length);
	if(actualRange)
		*actualRange = r;
	return [[self window] convertRectToScreen:_lastInputRect];
}

- (void)doCommandBySelector:(SEL)s {
	NSEvent *e;
	NSEventModifierFlags m;
	uint c, k;

	LOG(@"doCommandBySelector (%@)", NSStringFromSelector(s));

	/* Cocoa delivers Cmd+arrow and Option+arrow as selectors, not as key events.
	 * Map the selector directly to our rune so we don't depend on the event's keyCode/character. */
	if(s == @selector(moveToBeginningOfLine:) || s == @selector(moveToLeftEndOfLine:)){
		gfx_keystroke(self.client, Kcmdleft);
		return;
	}
	if(s == @selector(moveToEndOfLine:) || s == @selector(moveToRightEndOfLine:)){
		gfx_keystroke(self.client, Kcmdright);
		return;
	}
	if(s == @selector(moveToBeginningOfLineAndModifySelection:) || s == @selector(moveToLeftEndOfLineAndModifySelection:)){
		gfx_keystroke(self.client, Kshiftcmdleft);
		return;
	}
	if(s == @selector(moveToEndOfLineAndModifySelection:) || s == @selector(moveToRightEndOfLineAndModifySelection:)){
		gfx_keystroke(self.client, Kshiftcmdright);
		return;
	}
	if(s == @selector(moveWordLeft:) || s == @selector(moveWordBackward:)){
		gfx_keystroke(self.client, Kaltleft);
		return;
	}
	if(s == @selector(moveWordRight:) || s == @selector(moveWordForward:)){
		gfx_keystroke(self.client, Kaltright);
		return;
	}
	if(s == @selector(moveWordLeftAndModifySelection:) || s == @selector(moveWordBackwardAndModifySelection:)){
		gfx_keystroke(self.client, Kshiftaltleft);
		return;
	}
	if(s == @selector(moveWordRightAndModifySelection:) || s == @selector(moveWordForwardAndModifySelection:)){
		gfx_keystroke(self.client, Kshiftaltright);
		return;
	}
	if(s == @selector(insertBacktab:)){
		gfx_keystroke(self.client, Kshifttab);
		return;
	}

	e = [NSApp currentEvent];
	m = [e modifierFlags];
	switch([e keyCode]){
	case NSLeftArrowFunctionKey: k = Kleft; break;
	case NSRightArrowFunctionKey: k = Kright; break;
	case NSUpArrowFunctionKey: k = Kup; break;
	case NSDownArrowFunctionKey: k = Kdown; break;
	default:
		c = [[e characters] length] > 0 ? [[e characters] characterAtIndex:0] : 0;
		k = keycvt(c);
		break;
	}
	LOG(@"keyDown: keyCode %ld character0: 0x%x -> 0x%x", (long)[e keyCode], c, k);

	if(m & NSEventModifierFlagShift){
		if(k == Kleft) k = Kshiftleft;
		else if(k == Kright) k = Kshiftright;
		else if(k == Kup) k = Kshiftup;
		else if(k == Kdown) k = Kshiftdown;
	}
	if(m & NSEventModifierFlagCommand){
		if((m & NSEventModifierFlagShift) && 'a' <= k && k <= 'z')
			k += 'A' - 'a';
		if(' '<=k && k<='~')
			k += Kcmd;
	}
	if(k>0)
		gfx_keystroke(self.client, k);
}

// Helper for managing input rect approximately
- (void)resetLastInputRect {
	LOG(@"resetLastInputRect");
	_lastInputRect.origin.x = 0.0;
	_lastInputRect.origin.y = 0.0;
	_lastInputRect.size.width = 0.0;
	_lastInputRect.size.height = 0.0;
}

- (void)enlargeLastInputRect:(NSRect)r {
	r.origin.y = [self bounds].size.height - r.origin.y - r.size.height;
	_lastInputRect = NSUnionRect(_lastInputRect, r);
	LOG(@"update last input rect (%g, %g, %g, %g)",
		_lastInputRect.origin.x, _lastInputRect.origin.y,
		_lastInputRect.size.width, _lastInputRect.size.height);
}

- (void)clearInput {
	if(_tmpText.length){
		uint i;
		int l;
		l = 1 + _tmpText.length - NSMaxRange(_selectedRange)
			+ (_selectedRange.length > 0);
		LOG(@"move right %d", l);
		for(i = 0; i < l; ++i)
			gfx_keystroke(self.client, Kright);
		l = _tmpText.length+2+2*(_selectedRange.length > 0);
		LOG(@"backspace %d", l);
		for(uint i = 0; i < l; ++i)
			gfx_keystroke(self.client, Kbs);
	}
}

- (NSApplicationPresentationOptions)window:(id)arg
		willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)proposedOptions {
	// The default for full-screen is to auto-hide the dock and menu bar,
	// but the menu bar in particular comes back when the cursor is just
	// near the top of the screen, which makes acme's top tag line very difficult to use.
	// Disable the menu bar entirely.
	// In theory this code disables the dock entirely too, but if you drag the mouse
	// down far enough off the bottom of the screen the dock still unhides.
	// That's OK.
	NSApplicationPresentationOptions o;
	o = proposedOptions;
	o &= ~(NSApplicationPresentationAutoHideDock | NSApplicationPresentationAutoHideMenuBar);
	o |= NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
	return o;
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification {
	// This is a heavier-weight way to make sure the menu bar and dock go away,
	// but this affects all screens even though the app is running on full screen
	// on only one screen, so it's not great. The behavior from the
	// willUseFullScreenPresentationOptions seems to be enough for now.
	/*
	[[NSApplication sharedApplication]
		setPresentationOptions:NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock];
	*/
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
	/*
	[[NSApplication sharedApplication]
		setPresentationOptions:NSApplicationPresentationDefault];
	*/
}
@end

static uint
msec(void)
{
	return nsec()/1000000;
}

static uint
keycvt(uint code)
{
	switch(code){
	case '\r': return '\n';
	case 127: return '\b';
	case NSUpArrowFunctionKey: return Kup;
	case NSDownArrowFunctionKey: return Kdown;
	case NSLeftArrowFunctionKey: return Kleft;
	case NSRightArrowFunctionKey: return Kright;
	case NSInsertFunctionKey: return Kins;
	case NSDeleteFunctionKey: return Kdel;
	case NSHomeFunctionKey: return Khome;
	case NSEndFunctionKey: return Kend;
	case NSPageUpFunctionKey: return Kpgup;
	case NSPageDownFunctionKey: return Kpgdown;
	case NSF1FunctionKey: return KF|1;
	case NSF2FunctionKey: return KF|2;
	case NSF3FunctionKey: return KF|3;
	case NSF4FunctionKey: return KF|4;
	case NSF5FunctionKey: return KF|5;
	case NSF6FunctionKey: return KF|6;
	case NSF7FunctionKey: return KF|7;
	case NSF8FunctionKey: return KF|8;
	case NSF9FunctionKey: return KF|9;
	case NSF10FunctionKey: return KF|10;
	case NSF11FunctionKey: return KF|11;
	case NSF12FunctionKey: return KF|12;
	case NSBeginFunctionKey:
	case NSPrintScreenFunctionKey:
	case NSScrollLockFunctionKey:
	case NSF13FunctionKey:
	case NSF14FunctionKey:
	case NSF15FunctionKey:
	case NSF16FunctionKey:
	case NSF17FunctionKey:
	case NSF18FunctionKey:
	case NSF19FunctionKey:
	case NSF20FunctionKey:
	case NSF21FunctionKey:
	case NSF22FunctionKey:
	case NSF23FunctionKey:
	case NSF24FunctionKey:
	case NSF25FunctionKey:
	case NSF26FunctionKey:
	case NSF27FunctionKey:
	case NSF28FunctionKey:
	case NSF29FunctionKey:
	case NSF30FunctionKey:
	case NSF31FunctionKey:
	case NSF32FunctionKey:
	case NSF33FunctionKey:
	case NSF34FunctionKey:
	case NSF35FunctionKey:
	case NSPauseFunctionKey:
	case NSSysReqFunctionKey:
	case NSBreakFunctionKey:
	case NSResetFunctionKey:
	case NSStopFunctionKey:
	case NSMenuFunctionKey:
	case NSUserFunctionKey:
	case NSSystemFunctionKey:
	case NSPrintFunctionKey:
	case NSClearLineFunctionKey:
	case NSClearDisplayFunctionKey:
	case NSInsertLineFunctionKey:
	case NSDeleteLineFunctionKey:
	case NSInsertCharFunctionKey:
	case NSDeleteCharFunctionKey:
	case NSPrevFunctionKey:
	case NSNextFunctionKey:
	case NSSelectFunctionKey:
	case NSExecuteFunctionKey:
	case NSUndoFunctionKey:
	case NSRedoFunctionKey:
	case NSFindFunctionKey:
	case NSHelpFunctionKey:
	case NSModeSwitchFunctionKey: return 0;
	default: return code;
	}
}

// rpc_getsnarf reads the current pasteboard as a plain text string.
// Called from an RPC thread with no client lock held.
char*
rpc_getsnarf(void)
{
	char __block *ret;

	ret = nil;
	dispatch_sync(dispatch_get_main_queue(), ^(void) {
		@autoreleasepool {
			NSPasteboard *pb = [NSPasteboard generalPasteboard];
			NSString *s = [pb stringForType:NSPasteboardTypeString];
			if(s)
				ret = strdup((char*)[s UTF8String]);
		}
	});
	return ret;
}

// rpc_putsnarf writes the given text to the pasteboard.
// Called from an RPC thread with no client lock held.
void
rpc_putsnarf(char *s)
{
	if(s == nil || strlen(s) >= SnarfSize)
		return;

	dispatch_sync(dispatch_get_main_queue(), ^(void) {
		@autoreleasepool{
			NSArray *t = [NSArray arrayWithObject:NSPasteboardTypeString];
			NSPasteboard *pb = [NSPasteboard generalPasteboard];
			NSString *str = [[NSString alloc] initWithUTF8String:s];
			[pb declareTypes:t owner:nil];
			[pb setString:str forType:NSPasteboardTypeString];
		}
	});
}

// rpc_bouncemouse is for sending a mouse event
// back to the X11 window manager rio(1).
// Does not apply here.
static void
rpc_bouncemouse(Client *c, Mouse m)
{
}

// We don't use the graphics thread state during memimagedraw,
// so rpc_gfxdrawlock and rpc_gfxdrawunlock are no-ops.
void
rpc_gfxdrawlock(void)
{
}

void
rpc_gfxdrawunlock(void)
{
}

static void
setprocname(const char *s)
{
  CFStringRef process_name;

  process_name = CFStringCreateWithBytes(nil, (uchar*)s, strlen(s), kCFStringEncodingUTF8, false);

  // Adapted from Chrome's mac_util.mm.
  // http://src.chromium.org/viewvc/chrome/trunk/src/base/mac/mac_util.mm
  //
  // Copyright (c) 2012 The Chromium Authors. All rights reserved.
  //
  // Redistribution and use in source and binary forms, with or without
  // modification, are permitted provided that the following conditions are
  // met:
  //
  //    * Redistributions of source code must retain the above copyright
  // notice, this list of conditions and the following disclaimer.
  //    * Redistributions in binary form must reproduce the above
  // copyright notice, this list of conditions and the following disclaimer
  // in the documentation and/or other materials provided with the
  // distribution.
  //    * Neither the name of Google Inc. nor the names of its
  // contributors may be used to endorse or promote products derived from
  // this software without specific prior written permission.
  //
  // THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  // "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  // LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  // A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  // OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  // SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  // LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  // DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  // THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  // (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  // OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  // Warning: here be dragons! This is SPI reverse-engineered from WebKit's
  // plugin host, and could break at any time (although realistically it's only
  // likely to break in a new major release).
  // When 10.7 is available, check that this still works, and update this
  // comment for 10.8.

  // Private CFType used in these LaunchServices calls.
  typedef CFTypeRef PrivateLSASN;
  typedef PrivateLSASN (*LSGetCurrentApplicationASNType)();
  typedef OSStatus (*LSSetApplicationInformationItemType)(int, PrivateLSASN,
                                                          CFStringRef,
                                                          CFStringRef,
                                                          CFDictionaryRef*);

  static LSGetCurrentApplicationASNType ls_get_current_application_asn_func =
      NULL;
  static LSSetApplicationInformationItemType
      ls_set_application_information_item_func = NULL;
  static CFStringRef ls_display_name_key = NULL;

  static bool did_symbol_lookup = false;
  if (!did_symbol_lookup) {
    did_symbol_lookup = true;
    CFBundleRef launch_services_bundle =
        CFBundleGetBundleWithIdentifier(CFSTR("com.apple.LaunchServices"));
    if (!launch_services_bundle) {
      fprint(2, "Failed to look up LaunchServices bundle\n");
      return;
    }

    ls_get_current_application_asn_func =
        (LSGetCurrentApplicationASNType)(
            CFBundleGetFunctionPointerForName(
                launch_services_bundle, CFSTR("_LSGetCurrentApplicationASN")));
    if (!ls_get_current_application_asn_func)
      fprint(2, "Could not find _LSGetCurrentApplicationASN\n");

    ls_set_application_information_item_func =
        (LSSetApplicationInformationItemType)(
            CFBundleGetFunctionPointerForName(
                launch_services_bundle,
                CFSTR("_LSSetApplicationInformationItem")));
    if (!ls_set_application_information_item_func)
      fprint(2, "Could not find _LSSetApplicationInformationItem\n");

    CFStringRef* key_pointer = (CFStringRef*)(
        CFBundleGetDataPointerForName(launch_services_bundle,
                                      CFSTR("_kLSDisplayNameKey")));
    ls_display_name_key = key_pointer ? *key_pointer : NULL;
    if (!ls_display_name_key)
      fprint(2, "Could not find _kLSDisplayNameKey\n");

    // Internally, this call relies on the Mach ports that are started up by the
    // Carbon Process Manager.  In debug builds this usually happens due to how
    // the logging layers are started up; but in release, it isn't started in as
    // much of a defined order.  So if the symbols had to be loaded, go ahead
    // and force a call to make sure the manager has been initialized and hence
    // the ports are opened.
    ProcessSerialNumber psn;
    GetCurrentProcess(&psn);
  }
  if (!ls_get_current_application_asn_func ||
      !ls_set_application_information_item_func ||
      !ls_display_name_key) {
    return;
  }

  PrivateLSASN asn = ls_get_current_application_asn_func();
  // Constant used by WebKit; what exactly it means is unknown.
  const int magic_session_constant = -2;
  OSErr err =
      ls_set_application_information_item_func(magic_session_constant, asn,
                                               ls_display_name_key,
                                               process_name,
                                               NULL /* optional out param */);
  if(err != noErr)
    fprint(2, "Call to set process name failed\n");
}
