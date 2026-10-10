// Compiled with objc-arc to make it easier to avoid memory leaks

#include "EditorPlatform.hpp"

#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#import <os/lock.h>
#import <QuartzCore/CAMetalLayer.h>

namespace fx::editor::platform {

// Stupid ass language
static id sMouseMonitor = nil;

// Seconds after the last raw delta during which the (later arriving) event deltas for the same motion are dropped
static const CFTimeInterval scRawMouseHoldSeconds = 0.25;

// Raw counts have no pointer acceleration, so they are scaled to roughly match the event deltas
static const double scRawMouseScale = 1.0;

static os_unfair_lock sMouseLock = OS_UNFAIR_LOCK_INIT;

static bool sbRelativeMouse = false;

static double sEventDeltaX = 0.0;
static double sEventDeltaY = 0.0;

static double sRawDeltaX = 0.0;
static double sRawDeltaY = 0.0;
static CFTimeInterval sLastRawTime = 0.0;

static dispatch_queue_t sRawMouseQueue = nil;
static id sRawMouseObserver = nil;

static void AttachRawMouse(GCMouse* mouse) API_AVAILABLE(macos(11.0))
{
	mouse.handlerQueue = sRawMouseQueue;

	// Called on our own queue, so the deltas keep coming while the main thread is busy drawing the UI
	mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput*, float delta_x, float delta_y) {
	  os_unfair_lock_lock(&sMouseLock);

	  if (sbRelativeMouse) {
		  sRawDeltaX += delta_x * scRawMouseScale;
		  sRawDeltaY -= delta_y * scRawMouseScale;
		  sLastRawTime = CACurrentMediaTime();
	  }

	  os_unfair_lock_unlock(&sMouseLock);
	};
}

void InitRawMouse()
{
	if (sRawMouseQueue != nil) {
		return;
	}

	if (@available(macOS 11.0, *)) {
		sRawMouseQueue = dispatch_queue_create(
			"raptor.rawmouse", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,
																		   QOS_CLASS_USER_INTERACTIVE, 0));

		for (GCMouse* mouse in [GCMouse mice]) {
			AttachRawMouse(mouse);
		}

		sRawMouseObserver = [[NSNotificationCenter defaultCenter]
			addObserverForName:GCMouseDidConnectNotification
						object:nil
						 queue:[NSOperationQueue mainQueue]
					usingBlock:^(NSNotification* notification) {
					  AttachRawMouse((GCMouse*)notification.object);
					}];
	}
}

void* AttachMetalLayer(void* ns_view)
{
	NSView* view = (__bridge NSView*)ns_view;

	CAMetalLayer* layer = [CAMetalLayer layer];

	// Just matching my SDL shit here, no hi-dpi for now because I have bigger fish to fry
	layer.contentsScale = 1.0;

	// Setting the layer before wantsLayer makes this a layer hosting view, so AppKit leaves the layer's contents to us
	[view setLayer:layer];
	[view setWantsLayer:YES];

	return (__bridge void*)layer;
}

void SetRelativeMouseMode(void* ns_view, bool enabled)
{
	const bool is_enabled = (sMouseMonitor != nil);

	if (enabled == is_enabled) {
		return;
	}

	if (enabled) {
		NSView* view = (__bridge NSView*)ns_view;
		[[view window] setAcceptsMouseMovedEvents:YES];

		os_unfair_lock_lock(&sMouseLock);
		sbRelativeMouse = true;
		sEventDeltaX = 0.0;
		sEventDeltaY = 0.0;
		sRawDeltaX = 0.0;
		sRawDeltaY = 0.0;
		sLastRawTime = 0.0;
		os_unfair_lock_unlock(&sMouseLock);

		const NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged |
								 NSEventMaskOtherMouseDragged;

		// The cursor stays put while disassociated, but the events still carry how far the mouse moved
		sMouseMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
															  handler:^NSEvent*(NSEvent* event) {
																os_unfair_lock_lock(&sMouseLock);
																sEventDeltaX += event.deltaX;
																sEventDeltaY += event.deltaY;
																os_unfair_lock_unlock(&sMouseLock);
																return event;
															  }];

		CGAssociateMouseAndMouseCursorPosition(false);
		[NSCursor hide];
	}
	else {
		[NSEvent removeMonitor:sMouseMonitor];
		sMouseMonitor = nil;

		os_unfair_lock_lock(&sMouseLock);
		sbRelativeMouse = false;
		os_unfair_lock_unlock(&sMouseLock);

		CGAssociateMouseAndMouseCursorPosition(true);
		[NSCursor unhide];
	}
}

Vec2f ConsumeRelativeMouseDelta()
{
	os_unfair_lock_lock(&sMouseLock);

	// A mouse that reports raw deltas also produces events, so only one of the two may be used for a motion
	const bool use_raw = (sRawDeltaX != 0.0 || sRawDeltaY != 0.0) ||
						 (CACurrentMediaTime() - sLastRawTime) < scRawMouseHoldSeconds;

	const Vec2f delta(static_cast<float32>(use_raw ? sRawDeltaX : sEventDeltaX),
					  static_cast<float32>(use_raw ? sRawDeltaY : sEventDeltaY));

	sEventDeltaX = 0.0;
	sEventDeltaY = 0.0;
	sRawDeltaX = 0.0;
	sRawDeltaY = 0.0;

	os_unfair_lock_unlock(&sMouseLock);

	return delta;
}

void ActivateApp() { [NSApp activateIgnoringOtherApps:YES]; }

void DisableWindowTabbing() { [NSWindow setAllowsAutomaticWindowTabbing:NO]; }

bool IsAppActive() { return [NSApp isActive]; }

bool IsPreciseScrollEvent()
{
	NSEvent* event = [NSApp currentEvent];

	return event != nil && event.type == NSEventTypeScrollWheel && event.hasPreciseScrollingDeltas;
}

} // namespace fx::editor::platform
