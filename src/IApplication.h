#ifndef IAPPLICATION_GUARD
#define IAPPLICATION_GUARD

#pragma once

namespace RaytracingImplementation
{

	struct IApplication
	{
		virtual void OnInit() = 0;
		virtual void OnUpdate() = 0;
		virtual void OnRender() = 0;
		virtual void OnDestroy() = 0;

		// Samples override the event handlers to handle specific messages.
		virtual void OnKeyDown(UINT8 /*key*/) = 0;
		virtual void OnKeyUp(UINT8 /*key*/) = 0;

		// Accessors.
		virtual UINT GetWidth() const = 0;
		virtual UINT GetHeight() const = 0;
		virtual const WCHAR* GetTitle() const = 0;

		virtual void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc) = 0;
	};
}

#endif // !IAPPLICATION_GUARD