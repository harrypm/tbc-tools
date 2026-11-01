/************************************************************************

    testchromaconfigdialog.cpp

    Headless regression test for the chroma decoder configuration dialog's
    load sequence. Verifies that the decoder selection is absolute user
    config: when the metadata carries an explicit chromaDecoder string,
    the resetGui/updateGuiLoaded setConfiguration sequence (MainWindow passes
    isInit=true) must preserve the selection for EVERY decoder mode, and the
    emitted chromaDecoderConfigChanged carries the user's decoder - the value
    that flows into VideoParameters.chromaDecoder and the export's
    --chroma-decoder argument. Metadata with no chromaDecoder still gets the
    dialog's default selection (isInit=false: single source -> Transform 3D,
    split -> Transform 2D / ntsc3d/ntsc2d).

    Uses explicit verify() checks instead of assert(): the test target builds
    Release (-DNDEBUG), where assert() is compiled out and would pass vacuously.

    Copyright (C) 2026 Simon Inns

    This file is part of tbc-tools.

    tbc-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include <iostream>

#include <QApplication>
#include <QString>

#include "chromadecoderconfigdialog.h"
#include "tbcsource.h"

using std::cerr;

static bool g_failed = false;

static void verify(bool condition, const QString &what)
{
    if (!condition) {
        cerr << "FAIL: " << what.toStdString() << "\n";
        g_failed = true;
    }
}

static void testUserSelectionPreservedAcrossLoadSequence()
{
    cerr << "Testing: user chroma decoder selection survives the load sequence\n";

    TbcSource source;
    ChromaDecoderConfigDialog dialog;
    dialog.setTbcSource(&source);

    // PAL 2D selected by the user (metadata chromaDecoder = "pal2d"):
    // MainWindow::resetGui passes isInit=true; with the fix,
    // MainWindow::updateGuiLoaded also passes isInit=true because the
    // metadata carries an explicit chromaDecoder. The selection must be
    // preserved in both passes for single and split sources.
    {
        PalColour::Configuration palConfig;
        palConfig.chromaFilter = PalColour::palColourFilter; // PAL 2D
        Comb::Configuration ntscConfig;
        MonoDecoder::MonoConfiguration monoConfig;

        dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true); // resetGui pass
        dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true); // updateGuiLoaded pass
        verify(dialog.getPalConfiguration().chromaFilter == PalColour::palColourFilter,
               QStringLiteral("PAL 2D not preserved for ONE_SOURCE (got filter #%1)")
                   .arg(int(dialog.getPalConfiguration().chromaFilter)));

        dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                                TbcSource::BOTH_SOURCES, true); // resetGui pass
        dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                                TbcSource::BOTH_SOURCES, true); // updateGuiLoaded pass
        verify(dialog.getPalConfiguration().chromaFilter == PalColour::palColourFilter,
               QStringLiteral("PAL 2D not preserved for BOTH_SOURCES (got filter #%1)")
                   .arg(int(dialog.getPalConfiguration().chromaFilter)));
    }

    // NTSC 1D selected by the user (metadata chromaDecoder = "ntsc1d"):
    // dimensions must survive both load passes.
    {
        PalColour::Configuration palConfig;
        Comb::Configuration ntscConfig;
        ntscConfig.dimensions = 1;
        MonoDecoder::MonoConfiguration monoConfig;

        dialog.setConfiguration(NTSC, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true);
        dialog.setConfiguration(NTSC, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true);
        verify(dialog.getNtscConfiguration().dimensions == 1,
               QStringLiteral("NTSC 1D not preserved (got dimensions %1)")
                   .arg(dialog.getNtscConfiguration().dimensions));
    }

    // SECAM selected by the user (metadata chromaDecoder = "secam"): must
    // survive both load passes (previously the reset switched the filter to
    // Transform 2D, which maps to no SECAM decoder name at all).
    {
        PalColour::Configuration palConfig;
        palConfig.chromaFilter = PalColour::secam;
        Comb::Configuration ntscConfig;
        MonoDecoder::MonoConfiguration monoConfig;

        dialog.setConfiguration(SECAM, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true);
        dialog.setConfiguration(SECAM, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true);
        verify(dialog.getPalConfiguration().chromaFilter == PalColour::secam,
               QStringLiteral("SECAM not preserved (got filter #%1)")
                   .arg(int(dialog.getPalConfiguration().chromaFilter)));
    }
}

static void testDefaultSelectionStillAppliedWithoutMetadataDecoder()
{
    cerr << "Testing: default selection applies when metadata has no chromaDecoder\n";

    // No TbcSource here: the isInit=false branch reads
    // tbcSource->getVideoParameters() for the NTSC phase-compensation
    // default, which requires LOADED metadata (TbcMetaData::getVideoParameters
    // throws on an unread store). In the app this branch only ever runs with
    // a loaded source (updateGuiLoaded follows a successful load), so the
    // headless test exercises the no-source path instead.
    ChromaDecoderConfigDialog dialog;

    // Legacy metadata without a chromaDecoder string: MainWindow passes
    // isInit=false and the dialog selects its default (single source ->
    // Transform 3D, split -> Transform 2D).
    {
        PalColour::Configuration palConfig;
        palConfig.chromaFilter = PalColour::palColourFilter;
        Comb::Configuration ntscConfig;
        MonoDecoder::MonoConfiguration monoConfig;

        dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, false);
        verify(dialog.getPalConfiguration().chromaFilter == PalColour::transform3DFilter,
               QStringLiteral("PAL default for ONE_SOURCE should be transform3D (got filter #%1)")
                   .arg(int(dialog.getPalConfiguration().chromaFilter)));

        dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                                TbcSource::BOTH_SOURCES, false);
        verify(dialog.getPalConfiguration().chromaFilter == PalColour::transform2DFilter,
               QStringLiteral("PAL default for BOTH_SOURCES should be transform2D (got filter #%1)")
                   .arg(int(dialog.getPalConfiguration().chromaFilter)));
    }

    // NTSC default dimensions (single source -> 3D, split -> 2D).
    {
        PalColour::Configuration palConfig;
        Comb::Configuration ntscConfig;
        ntscConfig.dimensions = 1;
        MonoDecoder::MonoConfiguration monoConfig;

        dialog.setConfiguration(NTSC, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, false);
        verify(dialog.getNtscConfiguration().dimensions == 3,
               QStringLiteral("NTSC default for ONE_SOURCE should be 3 (got %1)")
                   .arg(dialog.getNtscConfiguration().dimensions));

        dialog.setConfiguration(NTSC, palConfig, ntscConfig, monoConfig,
                                TbcSource::BOTH_SOURCES, false);
        verify(dialog.getNtscConfiguration().dimensions == 2,
               QStringLiteral("NTSC default for BOTH_SOURCES should be 2 (got %1)")
                   .arg(dialog.getNtscConfiguration().dimensions));
    }
}

static void testAllChromaDecoderModesRoundTrip()
{
    cerr << "Testing: every chroma decoder mode survives the load sequence and round-trips its name\n";

    ChromaDecoderConfigDialog dialog;

    // (system, metadata decoder string, PAL filter, NTSC dimensions, NTSC nn,
    //  NTSC adaptive, canonical name after the load round trip). The dialog
    // configs mirror what TbcSource::applyChromaSettingsFromMetadata builds
    // from each metadata string.
    struct ModeExpectation {
        VideoSystem system;
        const char *decoder;
        PalColour::ChromaFilterMode palFilter;
        int ntscDimensions;
        bool ntscNn;
        bool ntscAdaptive;
        const char *canonicalName;
    };
    const ModeExpectation modes[] = {
        // PAL family
        { PAL, "pal2d", PalColour::palColourFilter, 2, false, true, "pal2d" },
        { PAL, "transform2d", PalColour::transform2DFilter, 2, false, true, "transform2d" },
        { PAL, "transform3d", PalColour::transform3DFilter, 2, false, true, "transform3d" },
        { PAL, "mono", PalColour::mono, 2, false, true, "mono" },
        // SECAM family
        { SECAM, "secam", PalColour::secam, 2, false, true, "secam" },
        { SECAM, "secam-predemod", PalColour::secamPredemod, 2, false, true, "secam-predemod" },
        { SECAM, "mono", PalColour::mono, 2, false, true, "mono" },
        // NTSC family
        { NTSC, "ntsc1d", PalColour::palColourFilter, 1, false, true, "ntsc1d" },
        { NTSC, "ntsc2d", PalColour::palColourFilter, 2, false, true, "ntsc2d" },
        { NTSC, "ntsc3d", PalColour::palColourFilter, 3, false, true, "ntsc3d" },
        { NTSC, "ntsc3dnoadapt", PalColour::palColourFilter, 3, false, false, "ntsc3dnoadapt" },
        { NTSC, "nntransform3d", PalColour::palColourFilter, 3, true, true, "nntransform3d" },
        { NTSC, "nntsc3d", PalColour::palColourFilter, 3, true, true, "nntransform3d" }, // alias canonicalises
        { NTSC, "mono", PalColour::palColourFilter, 0, false, true, "mono" },
    };

    for (const ModeExpectation &mode : modes) {
        PalColour::Configuration palConfig;
        palConfig.chromaFilter = mode.palFilter;
        Comb::Configuration ntscConfig;
        ntscConfig.dimensions = mode.ntscDimensions;
        ntscConfig.nnTransform3D = mode.ntscNn;
        ntscConfig.adaptive = mode.ntscAdaptive;
        MonoDecoder::MonoConfiguration monoConfig;

        // Replay the resetGui/updateGuiLoaded load sequence (both isInit=true
        // when the metadata carries a decoder string).
        dialog.setConfiguration(mode.system, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true);
        dialog.setConfiguration(mode.system, palConfig, ntscConfig, monoConfig,
                                TbcSource::ONE_SOURCE, true);

        const QString modeLabel = QString::fromLatin1(mode.decoder);

        // The dialog configuration must still represent the user's mode.
        const PalColour::Configuration livePal = dialog.getPalConfiguration();
        const Comb::Configuration liveNtsc = dialog.getNtscConfiguration();
        if (mode.system == PAL || mode.system == SECAM) {
            verify(livePal.chromaFilter == mode.palFilter,
                   QStringLiteral("%1: PAL/SECAM filter not preserved (got #%2)")
                       .arg(modeLabel).arg(int(livePal.chromaFilter)));
        } else {
            verify(liveNtsc.dimensions == mode.ntscDimensions,
                   QStringLiteral("%1: NTSC dimensions not preserved (got %2)")
                       .arg(modeLabel).arg(liveNtsc.dimensions));
            verify(liveNtsc.nnTransform3D == mode.ntscNn,
                   QStringLiteral("%1: NTSC nnTransform3D not preserved (got %2)")
                       .arg(modeLabel).arg(liveNtsc.nnTransform3D));
            if (mode.ntscDimensions == 3 && !mode.ntscNn) {
                verify(liveNtsc.adaptive == mode.ntscAdaptive,
                       QStringLiteral("%1: NTSC adaptive not preserved (got %2)")
                           .arg(modeLabel).arg(liveNtsc.adaptive));
            }
        }

        // The name the MainWindow handler writes into
        // VideoParameters.chromaDecoder must match the mode (never the hard
        // default). This is the value the export sends as --chroma-decoder.
        const QString derivedName = chromaDecoderNameFromConfig(mode.system, livePal, liveNtsc);
        verify(derivedName == QString::fromLatin1(mode.canonicalName),
               QStringLiteral("%1: derived decoder name should be '%2' but got '%3'")
                   .arg(modeLabel, QString::fromLatin1(mode.canonicalName), derivedName));
    }
}

static void testEmittedConfigCarriesUserSelection()
{
    cerr << "Testing: chromaDecoderConfigChanged carries the user's selection\n";

    TbcSource source;
    ChromaDecoderConfigDialog dialog;
    dialog.setTbcSource(&source);

    // The MainWindow handler derives VideoParameters.chromaDecoder from the
    // configuration the dialog reports on chromaDecoderConfigChanged. Replay
    // the full load sequence for a user-selected PAL 2D and capture what the
    // dialog reports when the signal fires.
    PalColour::Configuration palConfig;
    palConfig.chromaFilter = PalColour::palColourFilter; // PAL 2D
    Comb::Configuration ntscConfig;
    MonoDecoder::MonoConfiguration monoConfig;

    PalColour::ChromaFilterMode reportedFilter = PalColour::transform2DFilter;
    QObject::connect(&dialog, &ChromaDecoderConfigDialog::chromaDecoderConfigChanged,
                     &dialog, [&dialog, &reportedFilter]() {
        reportedFilter = dialog.getPalConfiguration().chromaFilter;
    });

    dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                            TbcSource::ONE_SOURCE, true); // resetGui pass
    verify(reportedFilter == PalColour::palColourFilter,
           QStringLiteral("resetGui pass signal did not carry PAL 2D (got filter #%1)")
               .arg(int(reportedFilter)));
    reportedFilter = PalColour::transform2DFilter;

    dialog.setConfiguration(PAL, palConfig, ntscConfig, monoConfig,
                            TbcSource::ONE_SOURCE, true); // updateGuiLoaded pass
    // The signal fired during the load must still report the user's PAL 2D,
    // not the hard default that used to overwrite it.
    verify(reportedFilter == PalColour::palColourFilter,
           QStringLiteral("updateGuiLoaded pass signal did not carry PAL 2D (got filter #%1)")
               .arg(int(reportedFilter)));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("testchromaconfigdialog"));

    testUserSelectionPreservedAcrossLoadSequence();
    testDefaultSelectionStillAppliedWithoutMetadataDecoder();
    testAllChromaDecoderModesRoundTrip();
    testEmittedConfigCarriesUserSelection();

    if (g_failed) {
        cerr << "testchromaconfigdialog FAILED\n";
        return 1;
    }
    cerr << "testchromaconfigdialog passed\n";
    return 0;
}
