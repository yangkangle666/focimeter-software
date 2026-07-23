# M3 Conservative Safety Design

## Goal

Make the first-stage M3 pipeline fail safely when its M2 spot correspondence or
calibration evidence is unreliable. The change must preserve the shared JSON
field names and must not claim real-device metrology capability.

## Scope

The implementation changes only `focimeter_system/modules/calibration_calculation/`.
It does not change the shared M2/M3 interface or add a new M2 tracking field.

## Correspondence Policy

Within the current M2 protocol, `spot_id` is inherited from the calibration
image only after a unique constrained geometric match succeeds. M3 will
therefore pair calibration and measurement spots by `spot_id`, then require the
paired `role` values to agree. Array detection order has no identity meaning.

Calculation-ready inputs must contain exactly five unique, known roles:
`center`, `y_positive`, `left_or_negative`, `other`, and `x_positive`.
Inputs with duplicate or `unknown` roles, different ID sets, or an ID whose role
changes across inputs are rejected. This consumes M2's current identity evidence
without claiming that a software-generated ID proves physical-ray tracking.

M3 will additionally reject fitted transforms that are reflected or reverse a
paired outer spot direction. These checks catch obvious slot swaps. They cannot
prove physical correspondence, so the README will state that the module is
limited to mock and synthetic integration until the current matching protocol is
approved and validated with paired real-device images, or another hardware or
tracking identity mechanism is approved.

## Geometry and Output Semantics

The calibration basis will match the C++ reference implementation:

1. `y_positive` defines the normalized Y axis.
2. X is the clockwise 90-degree rotation of Y.
3. `x_positive` selects the X sign.

The reported `shift_x_positive` and `shift_y_positive` values will be projected
into that basis. They will therefore match the existing `calibration_pixel`
output label rather than raw image-pixel coordinates.

## Metrology Data Isolation

M3 is a formula-based algorithm with calibration parameters, not a machine-learning
model. The legacy machine value `partition=train` denotes the calibration set.
Only that set may determine the correction matrix, cylinder threshold, and geometry
quality limits. The independent validation set determines the proposed operating
range. A final test set evaluates the frozen algorithm version against the gates.

Sample IDs must be unique. A lens `serial_number`, measurement JSON path, or
canonical measurement JSON content cannot occur across the calibration,
validation, and final test partitions. Repeated captures remain permitted only
within the single partition assigned to that physical lens.

The calibration artifact retains the existing `model_*` names and
`validation_status=metrology_validated` token for file-format compatibility. That
status means the algorithm version has passed metrology validation; it is not a
claim that M3 is a trained machine-learning model.

## Tests

Add regression coverage for ID-based pairing, invalid/unknown roles, reflected
or reversed geometry, C++-aligned non-orthogonal basis construction, calibrated
shift coordinates, serial-number and measurement leakage across all three
partitions, validation-only range metadata, and final-test evaluation. Existing
JSON contract tests and full M3 tests must remain green.

## Known Limit

The design deliberately favors false rejection over potentially wrong S/C/A.
It cannot establish real-device physical-ray identity from a software field name
alone. That claim requires approval of the geometric protocol and paired
real-device validation data, or a separately approved hardware/tracking mechanism.
