// Compiled with objc-arc to make it easier to avoid memory leaks

#include "EditorPlatform.hpp"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace fx::editor::platform {

// Stupid ass language
static id sMouseMonitor = nil;

static CGFloat sMouseDeltaX = 0.0;
static CGFloat sMouseDeltaY = 0.0;

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

		sMouseDeltaX = 0.0;
		sMouseDeltaY = 0.0;

		const NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged |
								 NSEventMaskOtherMouseDragged;

		// The cursor stays put while disassociated, but the events still carry how far the mouse moved
		sMouseMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
															  handler:^NSEvent*(NSEvent* event) {
																sMouseDeltaX += event.deltaX;
																sMouseDeltaY += event.deltaY;
																return event;
															  }];

		CGAssociateMouseAndMouseCursorPosition(false);
		[NSCursor hide];
	}
	else {
		[NSEvent removeMonitor:sMouseMonitor];
		sMouseMonitor = nil;

		CGAssociateMouseAndMouseCursorPosition(true);
		[NSCursor unhide];
	}
}

Vec2f ConsumeRelativeMouseDelta()
{
	const Vec2f delta(static_cast<float32>(sMouseDeltaX), static_cast<float32>(sMouseDeltaY));

	sMouseDeltaX = 0.0;
	sMouseDeltaY = 0.0;

	return delta;
}

void ActivateApp() { [NSApp activateIgnoringOtherApps:YES]; }

} // namespace fx::editor::platform
