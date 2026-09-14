# XRR Layer Studio CPP v2.5.1

XRR Layer Studio is a native 64-bit Windows C++ application for previewing Bruker D8 Advance X-ray reflectivity (XRR) data, estimating film thickness by FFT or two-point fringe spacing, simulating reflectivity from a layer stack, and fitting density, thickness, and roughness with a Genetic Algorithm. No installation is required: extract the release package and run `XRR_Layer_Studio_CPP_v2.5.1.exe`.

## Current features

- Opens one `.raw` or `.txt` measurement at a time and detects the format automatically.
- Supports the Bruker `RAW4.00` binary format represented by the included sample, and sectioned TXT exports containing `[RangeHeader]` and `[Data]` blocks.
- Plots `2θ (°)` on the horizontal axis and intensity on a true logarithmic vertical axis.
- Treats `-9999` records as unmeasured points. They are skipped during import, while the corresponding gaps remain disconnected in the plotted curve.
- Retains valid records with zero or negative intensity in projects and CSV data, but does not display them on the logarithmic plot.
- Adds no horizontal padding by default: the minimum and maximum valid 2θ points lie at the left and right plot boundaries. Enabling `Show full nominal scan range` explicitly includes any nominal but unmeasured range declared by the file.
- Disables mouse-wheel zoom. Drag a rectangle with the left mouse button to zoom both the 2θ and logarithmic-intensity ranges. Double-click the plot, click `Reset view`, or press `R` to restore the full view. Hover readout of 2θ and intensity remains available.
- Hold `Ctrl` and click two points on the experimental curve to mark points 1 and 2. A result dialog reports both 2θ values, Δ(2θ), Δqz, and the single-layer thickness obtained under the adjacent-Kiessig-fringe assumption.
- Displays measured data as a solid blue curve and the layer-stack model as a dashed orange curve. Use `Show layer-stack simulation` to hide or show the model.
- Provides `FFT thickness` beside the plot. The 2θ range is optional; leaving it blank uses the complete measured domain. The result window plots the thickness spectrum and lists candidate periodic thicknesses.
- Opens a material picker before `+ Above` or `+ Below` inserts a layer. It defaults to a blank custom layer and also provides 118 presets covering common elements, oxides, nitrides/carbides, semiconductors/functional compounds, and magnetic alloys.
- Filters material presets by category and by exact element symbol or English name, for example `Co`, `cobalt`, or `Si`. A preset fills only the material and reference density; thickness and roughness remain blank.
- Provides a separate fit-configuration dialog in which variables can be filtered by parameter type and layer type, selected individually, and assigned custom lower and upper bounds.
- Accepts an optional `2θ Min / Max` range for each fit. Blank limits use all valid data points.
- Offers the recommended `Robust Poisson · high-angle balanced` objective and the legacy-compatible `Pure Poisson deviance` objective.
- When a TXT file records an unmonochromated Cu Kα₂ wavelength and intensity ratio, simulation and fitting automatically include the incoherent Kα₁/Kα₂ sum.
- Exports PNG or SVG plots together with an automatically generated, same-name CSV file in the same directory. Plot data may also be exported separately. The CSV contains exactly three columns—2θ, experimental intensity, and fitted/simulated intensity—and never contains `gap_before`, `-9999`, or any other breakpoint column. If the layer model is incomplete, the third column is left blank.
- Displays `@KHSO5 All rights reserved.` at the bottom of the main window. This notice is excluded from exported PNG/SVG plots and is also hidden automatically when exporting the complete program window.
- Provides `Open project` directly on the main toolbar. `Reset view` is placed beside the plot, separate from project-file commands.
- Uses Per-Monitor V2 High-DPI scaling. The initial window, main layout, layer table, and fit dialogs scale for the current Windows display setting; fonts and control geometry are rebuilt when the window moves between monitors. The layer-stack panel uses a larger dedicated font.

## Layer stack and XRR simulation

The table on the right is ordered from the top surface to the substrate. The final `Substrate` row is fixed: it cannot be deleted or moved, its thickness is always blank, and no row can be inserted below it. Up to 256 film layers can be inserted above the substrate. The material picker initially selects `Custom / empty layer`, so pressing Enter preserves the blank-layer workflow; a preset can instead be found by search and inserted by double-clicking it.

Double-click a cell to enter:

- material or a note;
- density in `g/cm³`;
- expected thickness in `nm`;
- roughness in `nm`.

Roughness is displayed with at least three decimal places in the main table, fit configuration, and fit-result dialog. For example, `0.23` is displayed as `0.230 nm`. This is display formatting only; the fit and project file retain full numerical precision.

Numeric cells may temporarily remain blank. Density must be greater than zero; film thickness and roughness must be greater than or equal to zero. Use a period as the decimal separator, for example `12.37`. Each row's roughness describes the interface at the top of that layer. A blank roughness is treated as 0 nm.

The material entry must begin with a chemical formula such as `Ru`, `SiO2`, or `Co80Tb20`; a note may follow it, as in `Ru cap`. `Co80Tb20` is interpreted as an atomic ratio. For `Si / native oxide`, the leading `Si` formula is used in the calculation. A simulated curve appears only after every material and density, and every film thickness, has been completed.

Preset densities are nominal bulk or crystalline reference values near room temperature, shown to three decimals and always editable. Deposited films may differ substantially because of phase, alloy composition, porosity, oxygen content, or amorphous structure. Presets reduce lookup and data-entry effort; they do not replace sample-specific density priors or sensible fit bounds. Entries with strong phase or composition dependence are identified in the picker's `Reference note`.

The simulation uses an iterative, substrate-up Parratt calculation with Nevot–Croce interface-roughness factors. Electron density is calculated from the chemical formula and mass density. The imported X-ray wavelength is used. For the supplied PtCoTb TXT example, which reports `Alpha1=1.5406 Å`, `Alpha2=1.54439 Å`, and `AlphaRatio=0.5` without a monochromator/analyzer record, the two wavelengths are combined incoherently according to their intensity ratio. Simulated points reuse the imported 2θ coordinates and gaps exactly, so the range, step size, and unmeasured `-9999` regions follow the original data. The final model curve is normalized so its maximum equals the maximum imported intensity.

During fitting, the application also estimates a Gaussian-equivalent 2θ instrumental-resolution FWHM between 0 and 0.050°. Broadening uses a stable linear-time discrete diffusion convolution and does not cross `-9999` gaps. Each GA worker thread reuses its own contiguous arrays.

Before fitting, the displayed curve is the ideal specular simulation. The layer table, fit selections, Poisson nuisance model, instrumental broadening, dual-wavelength metadata, measurement data, and view are all stored in the `.xrrproj` file. Version 2.5.1 can open projects saved by versions 2.0 through 2.5.0. Material presets are only an entry aid and do not change the project format; once inserted, their material and density values are stored like ordinary manually entered cells.

## Plot navigation, two-point thickness, and data export

Drag inside the plot with the left mouse button to display a translucent rectangle. Releasing the button zooms to the corresponding 2θ and log(Intensity) ranges. Accidental selections smaller than eight logical pixels do not change the view, and mouse-wheel events do not zoom. In the default and `Reset view` states, the x-axis bounds are exactly the first and last positive-intensity measurement points, with no additional 2.5% padding.

Hold `Ctrl` and click near the blue experimental curve. The application snaps to the nearest visible experimental point within 26 logical pixels. The first point is marked with a purple `1` and the second with a green `2`. Once the second point is selected, thickness is calculated as

$$
q_z=\frac{4\pi}{\lambda}\sin\theta,\qquad
t=\frac{2\pi}{|q_{z,2}-q_{z,1}|}.
$$

The selected 2θ values are converted exactly to qz; the calculation does not use a simple small-angle approximation. The result assumes that the two points are adjacent Kiessig fringes from the same single layer, so $\Delta m=1$. If the selected points span $N$ fringe periods, multiply the reported thickness by $N$. After completing one pair, the next `Ctrl + click` starts a new pair.

The toolbar `Export plot` command and `File → Export plot as PNG/SVG` save a `.csv` beside the image using the same base name. For example, selecting `sample.png` also creates `sample.csv`. If that CSV already exists, the application asks separately before replacing it. `File → Export plot data CSV` remains available when only the data file is needed.

Every plot-data CSV contains exactly three columns:

1. `two_theta_deg`;
2. `experimental_intensity`;
3. `fitted_intensity`.

The third column contains the current model on the complete measurement-coordinate grid, regardless of whether the simulated curve is temporarily hidden on the plot. If no model is available, the third column is blank. Points excluded as `-9999` during import do not reappear, and plot gaps are not exported as an additional breakpoint column.

The release package includes `examples/XRR_15min_Ru8_plot_data_v2.5.1.csv`, a 3,604-row, three-column example generated from the included RAW file and the Ru/Co80Tb20/SiO2 reference stack.

## FFT thickness estimation

Click `FFT thickness` beside the plot or press `Ctrl+T`. The dialog accepts optional minimum and maximum 2θ values and a maximum thickness to display. Leaving both angle fields blank uses all positive-intensity measurement points. Filling only one limit applies a one-sided range. Interpolation never crosses a `-9999` gap.

To map Fourier frequency directly to real-space distance, the program first converts angle to

$$
q_z=\frac{4\pi}{\lambda}\sin\theta,\qquad \theta=\frac{2\theta}{2},
$$

then resamples onto a uniform qz grid, subtracts a quadratic trend from $\log_{10} I$, applies robust outlier limiting and a Hann window, and finally runs a zero-padded iterative radix-2 FFT. If $f_q$ is the frequency with respect to qz, the candidate periodic thickness is

$$
t=2\pi f_q.
$$

Internally, qz is expressed in Å⁻¹ and thickness is converted to nm. The result window reports the actual 2θ/qz range, FFT size, intrinsic thickness resolution determined by the qz span, and candidate peaks ranked by relative amplitude. Zero padding makes peak positions smoother to read but does not improve the intrinsic resolution.

An FFT peak represents a periodic interface-spacing component. It may correspond to a single-layer thickness, a sum or difference of several layers, or a higher harmonic; it is not a unique layer-stack solution. Use peaks as initial thickness estimates, then verify them against the known growth structure and Parratt/GA fitting.

## Genetic Algorithm fitting

Click `Fit selected variables...` on the right or press `Ctrl+F` to open the fit configuration:

- Density `ρ` and roughness `σ` are listed for every row, while thickness `t` is listed for each film. The substrate has no thickness parameter.
- The `Parameter` filter can show all parameters or only Density, Thickness, or Roughness. The `Layers` filter can show all rows, films only, or the substrate only.
- `Select shown / Clear shown` affects only variables visible under the current filters. Changing filters preserves the selection and bounds of temporarily hidden variables. The top-right summary reports visible, total, and selected counts.
- Check boxes determine which variables participate in the current fit. Unchecked values remain fixed.
- Double-click `Lower` or `Upper` to edit a bound. Defaults are −20% and +20% of the current value. When an initial thickness or roughness is zero, the default range is `0–0.2 nm` to avoid identical limits.
- `Fit 2θ range` minimum and maximum fields may be left blank. Two blank fields fit all valid points; one populated field creates a one-sided range. A range containing fewer than eight usable points is rejected.
- The current value must lie inside its bounds. Density lower bounds must be positive; thickness and roughness lower bounds cannot be negative.
- `Objective` defaults to Robust Poisson. Select Pure Poisson when a strict global maximum-likelihood objective is required.
- Up to 24 structural variables may be selected in one fit.

The optimizer is a bounded, real-coded, multithreaded Genetic Algorithm using tournament selection, BLX-type blended crossover, Gaussian mutation, elitism, and population injection after stagnation. Populations are stored in contiguous arrays, and Parratt calculations for individuals are distributed across available CPU cores. The search phase uses up to 1,500 valid measurement points sampled uniformly from the selected 2θ range. Final metrics use every valid point within that range, while the plotted result is rebuilt on the complete imported coordinate grid.

The underlying count-statistics quantity is Poisson deviance:

$$
D=2\sum_i\left[\mu_i-y_i+y_i\ln\left(\frac{y_i}{\mu_i}\right)\right],
$$

where $y_i$ is imported intensity and $\mu_i$ is the expected model count. Pure Poisson mode minimizes $D$ directly. The recommended Robust Poisson mode applies a Cauchy loss to each pointwise deviance residual:

$$
L_i=4\ln\left(1+\frac{D_i}{4}\right).
$$

Near the noise level, this retains the meaning of Poisson statistics, while limiting the ability of high-count, low-angle model mismatch to overwhelm weak high-angle fringes. The model also optimizes qz-dependent diffuse-scattering attenuation, a constant detector-background mean, and instrumental broadening. The maximum of the complete model curve remains normalized exactly to the imported maximum after these terms are applied.

A progress window is shown during fitting, and the fit can be cancelled. The result dialog reports every fitted parameter as initial value → result, its bounds, the selected objective, raw Poisson deviance, log-RMSE over the highest third of the fit range, evaluation count, elapsed time, diffuse parameters, detector mean, and resolution FWHM. Values are copied back to the layer table only after `Apply to layer table` is selected. `Discard result` or cancellation leaves the table unchanged. Manually editing the stack invalidates the previous Poisson nuisance parameters to prevent accidental reuse.

The diffuse term is an empirical attenuation model for one-dimensional specular XRR, not a two-dimensional off-specular DWBA calculation. The current release includes dual-wavelength and instrumental-resolution broadening, but does not yet include complex-SLD absorption, footprint correction, depth grading/interface intermixing, or parameter-uncertainty estimation. Density, thickness, and roughness may be correlated, so fit results should be assessed using sensible bounds and sample-specific prior knowledge.

## Routine workflow

1. Run the EXE, or drag a RAW/TXT file into the application window.
2. Use `Open data` to select a measurement. Use `Open project` on the main window to restore the complete state from an `.xrrproj` file. Opening a new measurement preserves the current layer table.
3. Drag a rectangle to zoom into a region of interest, or hold `Ctrl` and select two adjacent fringe points for a quick single-layer thickness estimate.
4. For spectrum analysis, click `FFT thickness`; leave the range blank to analyze the complete measured domain.
5. Select a row on the right and use `+ Above` or `+ Below` to insert a layer. Keep the blank custom layer or search for a material preset. The model recalculates automatically after all required parameters have been entered.
6. Click `Fit selected variables...`, select variables and bounds, and start the fit.
7. Review the result dialog. Parameters update only after confirmation. Use `Export plot data CSV` when numerical results are required; the third column contains the current fitted/simulated curve.
8. Use `Save project` to store the complete working state. A later `Open project` restores the curves, layer table, fit selections, Poisson terms, and plot view.

`examples/XRR_15min_Ru8_reference_fit.xrrproj` uses the supplied RAW data and the reference Ru(8.423 nm)/Co80Tb20(50.428 nm)/SiO2 structure. The two film thicknesses are selected by default, so the project can be inspected or fitted immediately after opening.

The model curve in the reference figure lies visibly above the experimental curve at high angle, so the tabulated values are not the strict optimum of the current simplified physical model. The reference project preserves those values exactly. If density, roughness, or additional variables are released, the Genetic Algorithm continues optimizing the selected objective and does not artificially lock the result to the reference values.

## PtCoTb high-angle validation

`PtCoTb.txt` contains 1,881 valid points from 0.6° to 10° with a 0.005° step and no `-9999` records. For the illustrated Ru/Co80Tb20/Pt/Ta/SiO2 stack, the legacy full-range Pure Poisson fit can drive the detector mean to approximately 6,507 counts and lift the entire 6–10° region. The recommended v2.3.0-and-later workflow is:

1. Fit thickness and roughness over the full range using Robust Poisson.
2. Keep density fixed and run a second fit from 6° to 10°, narrowing thickness bounds to approximately ±3% around the first result and applying physically sensible roughness bounds.

In validation, the log-RMSE values over 6–8° and 8–10° were 0.047 and 0.095 decades, respectively. The example project uses fixed densities of Ru 12.379, Co80Tb20 8.397, Pt 21.45, Ta 16.69, and SiO2 2.650 g/cm³. These values are validation assumptions and do not replace independent sample-specific information. See `examples/PtCoTb_high_angle_refined_v2.3.0.xrrproj` and `docs/PtCoTb_high_angle_validation_v2.3.0.png`.

The executable is not commercially code-signed. If Windows SmartScreen appears on first launch, verify the supplied SHA-256 value, select **More info**, and then choose **Run anyway**.

## Building from source

Install Visual Studio 2022 with the **Desktop development with C++** workload and CMake. Run `build_windows.bat`, or invoke it from an x64 Native Tools Command Prompt. The Release executable is written to the `bin` directory, with the MSVC runtime linked statically.

## Compatibility

This release targets Windows 10/11 x64. RAW is a vendor binary format; the current parser targets the `RAW4.00` structure represented by the supplied sample. Unsupported or damaged files produce an error instead of triggering a guessed data offset.

---

`@KHSO5 All rights reserved.`
