#pragma once
// Umbrella header for qlab::instr (spec 12): the instrument interface and registry, the models of
// the lab's measurement tools and signal sources, the routing matrix, live mode, the
// simulator-only probes and the mixer-calibration tool.
//
// Layering (SPEC_DEVIATIONS.md): Instruments does not depend on Runtime. The App fills
// instr::RunView and instr::Environment (Inputs.hpp) each frame and publishes them through the
// registry's InputHub; Lab binding providers are adapted from InstrumentRegistry::query().
#include "Instruments/Awg.hpp"
#include "Instruments/Controller.hpp"
#include "Instruments/DcSource.hpp"
#include "Instruments/Descriptor.hpp"
#include "Instruments/Digitizer.hpp"
#include "Instruments/Discriminator.hpp"
#include "Instruments/Gauges.hpp"
#include "Instruments/Generator.hpp"
#include "Instruments/Inputs.hpp"
#include "Instruments/Instrument.hpp"
#include "Instruments/InstrumentBase.hpp"
#include "Instruments/IqMixer.hpp"
#include "Instruments/Live.hpp"
#include "Instruments/MixerCal.hpp"
#include "Instruments/Oscilloscope.hpp"
#include "Instruments/Probes.hpp"
#include "Instruments/ReadoutChain.hpp"
#include "Instruments/Registry.hpp"
#include "Instruments/Settings.hpp"
#include "Instruments/Signal.hpp"
#include "Instruments/SpectrumAnalyzer.hpp"
#include "Instruments/StateAccess.hpp"
#include "Instruments/Thermometer.hpp"
#include "Instruments/Types.hpp"
#include "Instruments/Vna.hpp"
