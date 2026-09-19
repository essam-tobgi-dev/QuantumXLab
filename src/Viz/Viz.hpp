#pragma once
// Umbrella header for qlab::viz (spec 21) — the state, circuit, pulse and device visualizations.
//
//   ViewInput           what every view consumes: a qsim::Snapshot with the reductions computed for
//                       it, shot counts with the ideal distribution, the ir::Circuit at each
//                       compile stage, the pulse::Schedule with its playhead, hw::Device +
//                       hw::Calibration, Lindblad / trajectory series and the QEC views. The App
//                       fills it from the session (deviation from spec 21 §1: Viz does not depend
//                       on Runtime).
//   computeReductions   the off-thread reducer behind `IStateView::wants()`: reduced density
//                       matrices, entropies, pair mutual information, concurrence, Schmidt
//                       spectrum.
//   math::              pure presentation maths — Bloch vectors, phase → hue on the cyclic twilight
//                       LUT, Q-sphere placement, city / Hinton geometry, Wilson intervals, Wigner.
//   layout::            pure layout — circuit moments and the timed variant, device graphs, pulse
//                       channel rows.
//   IStateView          a view: identity, observability, fidelity class, update / draw / hitTest.
//   StateView           the shared half of one: change detection, hover, click → SelectionModel,
//                       keyboard, qubit subset. Concrete views are in Viz/Views.
//   SelectionModel      the one app-level selection shared with the lab and the inspector (§1.1).
//   GlBackend/GlCanvas  the offscreen 4× MSAA canvas the GL views draw into (§1.2).
//   VizTheme, widgets:: theme tokens, header badges, readout card, phase wheel, colour bars.
//
// ImGui and ImPlot are used only inside this module's .cpp files; nothing here exposes them.
#include "Viz/GlCanvas.hpp"
#include "Viz/IStateView.hpp"
#include "Viz/Layout/CircuitLayout.hpp"
#include "Viz/Layout/GraphLayout.hpp"
#include "Viz/Layout/PulseLayout.hpp"
#include "Viz/Math/Amplitudes.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/DensityPlot.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/PauliTable.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Math/QSphere.hpp"
#include "Viz/Math/Reduced.hpp"
#include "Viz/Math/Statistics.hpp"
#include "Viz/Math/Wigner.hpp"
#include "Viz/Reductions.hpp"
#include "Viz/Selection.hpp"
#include "Viz/StateView.hpp"
#include "Viz/Theme.hpp"
#include "Viz/Types.hpp"
#include "Viz/ViewInput.hpp"
#include "Viz/Views/AmplitudeView.hpp"
#include "Viz/Views/BlochView.hpp"
#include "Viz/Views/CircuitView.hpp"
#include "Viz/Views/CityView.hpp"
#include "Viz/Views/GraphView.hpp"
#include "Viz/Views/HintonView.hpp"
#include "Viz/Views/HistogramView.hpp"
#include "Viz/Views/PauliView.hpp"
#include "Viz/Views/PhaseDiskView.hpp"
#include "Viz/Views/PulseView.hpp"
#include "Viz/Views/QSphereView.hpp"
#include "Viz/Views/ReducedView.hpp"
#include "Viz/Views/SchmidtView.hpp"
#include "Viz/Views/StateSource.hpp"
#include "Viz/Views/TimeSeriesViews.hpp"
#include "Viz/Views/WignerView.hpp"
#include "Viz/Widgets.hpp"
#include <memory>
#include <vector>

namespace qlab::viz {

// The spec 21 §3 catalog in the order the workspace presets list it (spec 19 §3). The App owns the
// instances, gives each the shared `SelectionModel`, and shows the ones its preset selects;
// `Observability::SimulatorOnly` views are hidden by the "Physical lab" preset.
std::vector<std::unique_ptr<IStateView>> makeAllViews();

// One view by its `id()` ("bloch", "qsphere", …), or null when the id is not in the catalog.
std::unique_ptr<IStateView> makeView(std::string_view id);

// Ids of the catalog, in the same order as `makeAllViews()`.
std::span<const std::string_view> viewIds();

} // namespace qlab::viz
