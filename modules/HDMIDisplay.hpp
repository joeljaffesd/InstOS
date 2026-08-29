#pragma once

#include <InstOS.hpp>
#include <circle/2dgraphics.h>
#include <circle/bcmframebuffer.h>
#include <circle/bcmpropertytags.h>
#include <circle/util.h>

namespace InstOS {

// HDMI display module: discovers the connected sink at init, logs its
// identity, and (unless constructed with bInfoOnly) brings up a
// double-buffered C2DGraphics drawing surface at the panel's EDID-preferred
// mode — the firmware's default framebuffer is often scaled (e.g. 1824x984 on
// a native 1600x600 panel), so the EDID mode is the one that maps 1:1.
//
// CM4-NANO-B notes: one mini-HDMI connector, 4K30 max. The firmware probes
// HDMI/EDID at boot only — a display must be connected before power-on.
class HDMIDisplay : public Module {
public:
	HDMIDisplay(boolean bInfoOnly = FALSE) : mInfoOnly(bInfoOnly) {}

	const char* name() const override { return "hdmi"; }

	// nullptr until init() has run with a display connected
	C2DGraphics* graphics() { return mpGraphics; }
	unsigned width() const { return mWidth; }
	unsigned height() const { return mHeight; }

	boolean init() override {
		unsigned nDisplays = CBcmFrameBuffer::GetNumDisplays();
		CLogger::Get()->Write(name(), LogNotice, "Firmware reports %u display(s)", nDisplays);

		for (unsigned nDisplay = 0; nDisplay < nDisplays; nDisplay++) {
			logDimensions(nDisplay);
		}

		readEDID(); // logs identity, sets mWidth/mHeight to the preferred mode

		if (mInfoOnly) {
			return TRUE;
		}

		if (nDisplays == 0) {
			CLogger::Get()->Write(name(), LogError, "No display connected at boot");
			return FALSE;
		}

		if (mWidth == 0 || mHeight == 0) {
			// no (usable) EDID: fall back to the firmware's default mode
			mWidth = mFirmwareWidth;
			mHeight = mFirmwareHeight;
		}

		mpGraphics = new C2DGraphics(mWidth, mHeight, TRUE, 0);
		if (!mpGraphics->Initialize()) {
			CLogger::Get()->Write(name(), LogError, "Cannot initialize framebuffer");
			delete mpGraphics;
			mpGraphics = nullptr;
			return FALSE;
		}

		CLogger::Get()->Write(name(), LogNotice, "Drawing surface: %u x %u (double-buffered)",
				      mpGraphics->GetWidth(), mpGraphics->GetHeight());
		return TRUE;
	}

private:
	void logDimensions(unsigned nDisplay) {
		CBcmPropertyTags Tags;

		// route subsequent framebuffer queries to this display (Pi 4 family)
		TPropertyTagSimple TagDisplayNum;
		TagDisplayNum.nValue = nDisplay;
		Tags.GetTag(PROPTAG_SET_DISPLAY_NUM, &TagDisplayNum, sizeof TagDisplayNum, 4);

		TPropertyTagDisplayDimensions TagDimensions;
		if (Tags.GetTag(PROPTAG_GET_DISPLAY_DIMENSIONS, &TagDimensions, sizeof TagDimensions)
		    && (TagDimensions.nWidth != 0 || TagDimensions.nHeight != 0)) {
			CLogger::Get()->Write(name(), LogNotice, "Display %u: %u x %u",
					      nDisplay, TagDimensions.nWidth, TagDimensions.nHeight);
			if (nDisplay == 0) {
				mFirmwareWidth = TagDimensions.nWidth;
				mFirmwareHeight = TagDimensions.nHeight;
			}
		} else {
			CLogger::Get()->Write(name(), LogNotice, "Display %u: not connected", nDisplay);
		}
	}

	// EDID block 0: the sink's identity as cached by the firmware at boot
	void readEDID() {
		CBcmPropertyTags Tags;
		TPropertyTagEDIDBlock TagEDID;
		TagEDID.nBlockNumber = EDID_FIRST_BLOCK;
		if (!Tags.GetTag(PROPTAG_GET_EDID_BLOCK, &TagEDID, sizeof TagEDID, 4)
		    || TagEDID.nStatus != EDID_STATUS_SUCCESS) {
			CLogger::Get()->Write(name(), LogWarning,
					      "No EDID available (no sink connected at boot?)");
			return;
		}

		const u8* pEDID = TagEDID.Block;

		static const u8 Header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
		if (memcmp(pEDID, Header, sizeof Header) != 0) {
			CLogger::Get()->Write(name(), LogWarning, "EDID header invalid");
			return;
		}

		// manufacturer: three 5-bit letters, big-endian, 'A' = 1
		u16 nManufacturer = static_cast<u16>((pEDID[8] << 8) | pEDID[9]);
		char Manufacturer[4] = {
			static_cast<char>('A' - 1 + ((nManufacturer >> 10) & 0x1F)),
			static_cast<char>('A' - 1 + ((nManufacturer >> 5) & 0x1F)),
			static_cast<char>('A' - 1 + (nManufacturer & 0x1F)),
			'\0'
		};

		u16 nProduct = static_cast<u16>(pEDID[10] | (pEDID[11] << 8));
		CLogger::Get()->Write(name(), LogNotice,
				      "EDID: %s product 0x%04X (week %u, %u), EDID v%u.%u",
				      Manufacturer, nProduct, pEDID[16], 1990 + pEDID[17],
				      pEDID[18], pEDID[19]);

		// preferred timing: first 18-byte descriptor at offset 54
		const u8* pDTD = &pEDID[54];
		unsigned nPixelClock = (pDTD[0] | (pDTD[1] << 8)) * 10; // kHz
		if (nPixelClock != 0) {
			mWidth = pDTD[2] | ((pDTD[4] & 0xF0) << 4);
			mHeight = pDTD[5] | ((pDTD[7] & 0xF0) << 4);
			CLogger::Get()->Write(name(), LogNotice,
					      "Preferred mode: %u x %u @ %u.%02u MHz pixel clock",
					      mWidth, mHeight,
					      nPixelClock / 1000, (nPixelClock % 1000) / 10);
		}

		// monitor name lives in a display descriptor tagged 0xFC
		for (unsigned nDesc = 1; nDesc <= 3; nDesc++) {
			const u8* pDesc = &pEDID[54 + 18 * nDesc];
			if (pDesc[0] == 0 && pDesc[1] == 0 && pDesc[3] == 0xFC) {
				char Name[14];
				unsigned i;
				for (i = 0; i < 13 && pDesc[5 + i] != '\n'; i++) {
					Name[i] = static_cast<char>(pDesc[5 + i]);
				}
				Name[i] = '\0';
				CLogger::Get()->Write(name(), LogNotice, "Monitor name: %s", Name);
				break;
			}
		}
	}

	boolean mInfoOnly;
	C2DGraphics* mpGraphics = nullptr;
	unsigned mWidth = 0;
	unsigned mHeight = 0;
	unsigned mFirmwareWidth = 0;
	unsigned mFirmwareHeight = 0;
};

} // namespace InstOS
