const state = {
  step: 1,
  bootstrap: null,
  calibration: "",
  measurement: "",
  configPath: "",
  config: null,
  result: null,
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

const multispotSimulationPreset = {
  calibration: "data/synthetic/generated_images/hartmann_reference.png",
  measurement: "data/synthetic/generated_images/hartmann_measurement.png",
  config: "config/default_config.json",
};

const legacyFiveSpotPreset = {
  calibration: "data/samples/calibration/calib_mock_001.jpg",
  measurement: "data/samples/measurement/meas_mock_001.jpg",
  config: "config/legacy_five_spot_config.json",
};

const HIDDEN_CONFIG_SECTIONS = new Set([
  "parameter_provenance",
  "data_profile",
  "calibration_reference",
]);

const FIXED_CONFIG_FIELDS = new Set([
  "calculation.angle_unit",
  "calculation.diopter_unit",
  "path_policy.path_type",
  "path_policy.allow_absolute_path",
  "coordinate_system.coordinate_type",
  "coordinate_system.origin",
  "coordinate_system.x_positive",
  "coordinate_system.y_positive",
  "coordinate_system.y_flip",
  "coordinate_system.confirmation_status",
  "hartmann_calibration.spacing_source",
  "hartmann_calibration.spacing_formula",
]);

const configLabels = {
  camera: "相机",
  camera_simulation: "相机模拟参数",
  optical: "光学",
  image_processing: "图像处理",
  recognition: "光斑识别",
  calculation: "计算单位",
  path_policy: "路径策略",
  coordinate_system: "坐标系",
  illumination: "照明光源",
  hartmann_calibration: "哈特曼像素标定",
  measurement_targets: "FL-800 测量目标",
  data_profile: "数据与验证状态",
  parameter_provenance: "参数来源与适用范围",
  calibration_reference: "标定参数引用",
};

const configFieldLabels = {
  pixel_size_um: { label: "像素尺寸", hint: "工业相机参考模拟值，后续由硬件参数替换", unit: "μm" },
  image_width: { label: "图像宽度", hint: "工业相机参考模拟值，后续由硬件参数替换", unit: "pixel" },
  image_height: { label: "图像高度", hint: "工业相机参考模拟值，后续由硬件参数替换", unit: "pixel" },
  parameter_status: { label: "参数状态", hint: "provisional 表示临时联调参数，不可用于精度结论", options: { simulated: "simulated · 模拟值", provisional: "provisional · 临时值", measured: "measured · 实测值" } },
  color_mode: { label: "成像模式", hint: "当前实图为 RGB；算法可按需要提取绿色或转为灰度", options: { mono: "mono · 黑白", rgb: "rgb · 彩色" } },
  bit_depth: { label: "位深", hint: "模拟相机灰度位深", unit: "bit" },
  exposure_min_ms: { label: "最小曝光时间", hint: "模拟相机曝光范围下限", unit: "ms" },
  exposure_max_ms: { label: "最大曝光时间", hint: "模拟相机曝光范围上限", unit: "ms" },
  image_plane_width_mm: { label: "像面宽度", hint: "参考工业相机像面尺寸模拟值", unit: "mm" },
  image_plane_height_mm: { label: "像面高度", hint: "参考工业相机像面尺寸模拟值", unit: "mm" },
  distance_m: { label: "光学传播距离", hint: "Hartmann 阵列到传感器的距离", unit: "m" },
  hartmann_spacing_mm: { label: "Hartmann 点阵间距", hint: "当前仅为传感器像面临时派生值；物面或孔距换算需要系统倍率", unit: "mm" },
  roi_width_ratio: { label: "ROI 宽度比例", hint: "图像横向保留区域", unit: "比例" },
  roi_height_ratio: { label: "ROI 高度比例", hint: "图像纵向保留区域", unit: "比例" },
  median_kernel: { label: "中值滤波核", hint: "必须为正奇数", unit: "尺寸" },
  tophat_kernel: { label: "顶帽滤波核", hint: "用于增强光斑区域", unit: "尺寸" },
  otsu_a: { label: "Otsu 下阈值", hint: "归一化阈值下界", unit: "0–1" },
  otsu_b: { label: "Otsu 上阈值", hint: "归一化阈值上界", unit: "0–1" },
  max_depth: { label: "最大处理深度", hint: "图像处理允许的深度", unit: "层" },
  spot_count_mode: { label: "光斑数量模式", hint: "正式多光斑使用自动检测，五光斑仅用于兼容", options: { auto: "auto · 自动检测", fixed: "fixed · 固定数量" } },
  expected_spot_count: { label: "期望光斑数量", hint: "标准点阵中的光斑数量", unit: "个" },
  min_confidence: { label: "最低识别置信度", hint: "低于此值的识别结果会被关注", unit: "0–1" },
  pixel_threshold: { label: "像素位移阈值", hint: "允许的最小像素变化", unit: "pixel" },
  angle_unit: { label: "角度单位", hint: "接口固定使用 degree", options: { degree: "degree · 度" } },
  diopter_unit: { label: "屈光度单位", hint: "接口固定使用 D", options: { D: "D · 屈光度" } },
  path_type: { label: "路径类型", hint: "所有输入路径相对于项目根目录", options: { relative_to_project_root: "项目根目录相对路径" } },
  allow_absolute_path: { label: "允许绝对路径", hint: "为保护项目边界，必须关闭" },
  coordinate_type: { label: "坐标类型", hint: "项目统一接口使用笛卡尔坐标", options: { cartesian: "cartesian · 笛卡尔" } },
  origin: { label: "坐标原点", hint: "图像坐标原点位置", options: { top_left: "top_left · 左上角" } },
  x_positive: { label: "X 正方向", hint: "图像横向正方向", options: { right: "right · 向右" } },
  y_positive: { label: "Y 正方向", hint: "图像纵向正方向", options: { down: "down · 向下" } },
  y_flip: { label: "Y 轴翻转", hint: "硬件坐标尚待确认，当前不翻转" },
  confirmation_status: { label: "硬件确认状态", hint: "坐标定义等待硬件最终确认", options: { pending_hardware: "pending_hardware · 待硬件确认", confirmed: "confirmed · 已确认" } },
  source_color: { label: "光源颜色", hint: "本次设备配置使用绿光，具体波长待硬件提供", options: { green_led: "green_led · 绿光 LED", green: "green · 绿光（兼容）" } },
  wavelength_nm: { label: "绿光波长", hint: "未提供具体波长，暂时待确认", unit: "nm" },
  spacing_source: { label: "哈特曼间距来源", hint: "通过相机检测到的光斑像素间距换算", options: { camera_pixel_spacing: "相机光斑像素间距" } },
  spot_spacing_px: { label: "光斑像素间距", hint: "由 M2 检测相邻光斑中心距离后填写", unit: "pixel" },
  spacing_formula: { label: "像素换算公式", hint: "仅得到传感器像面临时毫米值；不能直接证明物面或 Hartmann 孔距" },
  sphere_min_d: { label: "球镜最小值", unit: "D" },
  sphere_max_d: { label: "球镜最大值", unit: "D" },
  sphere_steps_d: { label: "球镜可选步长", unit: "D" },
  cylinder_min_d: { label: "柱镜最小值", unit: "D" },
  cylinder_max_d: { label: "柱镜最大值", unit: "D" },
  cylinder_steps_d: { label: "柱镜可选步长", unit: "D" },
  prism_min_delta: { label: "棱镜最小值", unit: "△" },
  prism_max_delta: { label: "棱镜最大值", unit: "△" },
  prism_step_delta: { label: "棱镜步长", unit: "△" },
  axis_min_degree: { label: "轴向最小值", unit: "degree" },
  axis_max_degree: { label: "轴向最大值", unit: "degree" },
  axis_step_degree: { label: "轴向步长", unit: "degree" },
  addition_min_d: { label: "下加度最小值", unit: "D" },
  addition_max_d: { label: "下加度最大值", unit: "D" },
  addition_steps_d: { label: "下加度可选步长", unit: "D" },
  uv_min_percent: { label: "UV 透过率最小值", unit: "%" },
  uv_max_percent: { label: "UV 透过率最大值", unit: "%" },
  uv_steps_percent: { label: "UV 透过率可选步长", unit: "%" },
  data_source: { label: "数据来源", hint: "区分合成、接口模拟和真实硬件数据", options: { synthetic: "synthetic · 合成数据", mock: "mock · 接口模拟", real: "real · 真实硬件" } },
  validation_status: { label: "验证状态", hint: "当前输出达到的验证等级", options: { simulation_only: "simulation_only · 仅模拟", software_verified: "software_verified · 软件验证", metrology_validated: "metrology_validated · 计量验证" } },
  hardware_parameters_confirmed: { label: "硬件参数已确认", hint: "只有真实硬件参数完成确认后才能开启" },
  metrology_validated: { label: "计量验证完成", hint: "当前必须为 false；软件联调结果不得写入精度结论" },
  usable_for: { label: "可用于", hint: "当前版本只允许 software_integration", options: { software_integration: "software_integration · 软件联调", metrology_validation: "metrology_validation · 计量验证" } },
  camera_pixel_size_um: { label: "像元尺寸来源", hint: "4.8 μm 当前为 mock/provisional，后续替换为硬件实测值", options: { mock_provisional: "mock_provisional · 临时模拟", hardware_measured: "hardware_measured · 硬件实测" } },
  object_plane_spacing_requires_optical_magnification: { label: "物面换算需要系统倍率", hint: "物面或 Hartmann 孔距不能只依靠像元尺寸计算" },
  calibration_file: { label: "标定文件", hint: "将随 M1 → M2 联调包一起打包" },
  calibration_version: { label: "标定版本", hint: "必须与标定文件中的版本一致" },
};

const sectionFieldLabels = {
  "calibration_reference.parameter_status": { label: "参数状态", hint: "provisional 为临时联调值，measured 为硬件实测值", options: { simulated: "simulated · 模拟值", provisional: "provisional · 临时值", measured: "measured · 实测值" } },
  "parameter_provenance.spot_spacing_px": { label: "像素间距来源", hint: "当前 215.398 px 由标定图直接测得", options: { measured_from_image: "measured_from_image · 图像实测", mock_simulated: "mock_simulated · 模拟", pending_image_measurement: "pending_image_measurement · 待测" } },
  "parameter_provenance.hartmann_spacing_mm": { label: "毫米间距来源", hint: "当前 1.03391 mm 是传感器像面临时派生值", options: { provisional_derived_sensor_plane: "provisional_derived_sensor_plane · 像面临时派生", hardware_calibrated: "hardware_calibrated · 硬件标定", not_available: "not_available · 暂无" } },
};

function createTaskId() {
  const now = new Date();
  const pad = (value) => String(value).padStart(2, "0");
  return `m1_${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
}

async function api(path, options = {}) {
  const response = await fetch(path, options);
  const data = await response.json().catch(() => ({ status: "error", error: { message: "服务返回了无法读取的内容。" } }));
  if (!response.ok) throw new Error(data.error?.message || `请求失败（${response.status}）`);
  return data;
}

async function initialize() {
  $("#task-id").value = createTaskId();
  bindEvents();
  try {
    state.bootstrap = await api("/api/bootstrap");
    document.querySelector(".connection").classList.add("online");
    $("#connection-text").textContent = "本地服务已连接";
    populateSelect("#calibration-existing", state.bootstrap.files.calibration, "请选择标定图");
    populateSelect("#measurement-existing", state.bootstrap.files.measurement, "请选择测量图");
    populateSelect("#config-existing", state.bootstrap.files.config, "请选择配置");
    state.config = structuredClone(state.bootstrap.default_config);
    state.configPath = "config/default_config.json";
    $("#config-existing").value = state.configPath;
    updatePresetAvailability();
    renderConfigFields();
    renderValidationState();
    renderRecentTasks();
    updateMirror();
  } catch (error) {
    $("#connection-text").textContent = "本地服务未连接";
    showMessage(error.message);
  }
}

function bindEvents() {
  $$(".step-button").forEach((button) => button.addEventListener("click", () => goToStep(Number(button.dataset.step))));
  $("#previous-step").addEventListener("click", () => goToStep(state.step - 1));
  $("#next-step").addEventListener("click", () => goToStep(state.step + 1));
  $("#task-id").addEventListener("input", updateMirror);
  $("#operator").addEventListener("input", updateMirror);
  $("#notes").addEventListener("input", updateMirror);
  $("#calibration-existing").addEventListener("change", (event) => selectPath("calibration", event.target.value));
  $("#measurement-existing").addEventListener("change", (event) => selectPath("measurement", event.target.value));
  $("#config-existing").addEventListener("change", (event) => loadConfig(event.target.value));
  $("#calibration-upload").addEventListener("change", (event) => uploadFile("calibration", event.target.files[0]));
  $("#measurement-upload").addEventListener("change", (event) => uploadFile("measurement", event.target.files[0]));
  $("#config-upload").addEventListener("change", (event) => uploadFile("config", event.target.files[0]));
  $("#run-m1").addEventListener("click", runM1);
  $("#new-task").addEventListener("click", resetTask);
  $("#multispot-simulation").addEventListener("click", prepareMultispotSimulation);
  $("#legacy-five-spot").addEventListener("click", prepareStageOneFiveSpot);
  $("#download-bundle").addEventListener("click", downloadBundle);
  $("#copy-bundle-note").addEventListener("click", copyBundleNote);
  $$(".tab").forEach((tab) => tab.addEventListener("click", () => selectTab(tab.dataset.tab)));
}

function populateSelect(selector, values, placeholder) {
  const select = $(selector);
  select.replaceChildren(new Option(placeholder, ""), ...values.map((value) => new Option(value, value)));
}

function presetReady(preset) {
  return state.bootstrap.files.calibration.includes(preset.calibration)
    && state.bootstrap.files.measurement.includes(preset.measurement)
    && state.bootstrap.files.config.includes(preset.config);
}

function updatePresetAvailability() {
  const multispot = $("#multispot-simulation");
  const legacy = $("#legacy-five-spot");
  multispot.disabled = !presetReady(multispotSimulationPreset);
  legacy.disabled = !presetReady(legacyFiveSpotPreset);
  multispot.title = multispot.disabled ? "项目中缺少多光斑模拟输入或配置" : "自动填充 LM700 / Hartmann 多光斑联调输入";
  legacy.title = legacy.disabled ? "项目中缺少五光斑兼容输入或配置" : "自动填充历史五光斑兼容输入";
}

async function readConfig(path) {
  const response = await fetch(`/api/file?${new URLSearchParams({ path })}`);
  if (!response.ok) throw new Error("无法读取配置文件。");
  return response.json();
}

async function applyPreset(preset, notes) {
  if (!state.bootstrap || !presetReady(preset)) throw new Error("联调预设引用的文件不完整。");
  const config = await readConfig(preset.config);

  state.result = null;
  state.calibration = preset.calibration;
  state.measurement = preset.measurement;
  state.config = config;
  state.configPath = preset.config;

  $("#task-id").value = createTaskId();
  $("#notes").value = notes;
  $("#calibration-existing").value = state.calibration;
  $("#measurement-existing").value = state.measurement;
  $("#config-existing").value = state.configPath;
  $("#calibration-current").textContent = state.calibration;
  $("#measurement-current").textContent = state.measurement;
  updateBundleActions(null);
  renderConfigFields();
  renderValidationState();
  updateMirror();
  goToStep(5, true);
}

async function prepareMultispotSimulation() {
  try {
    await applyPreset(
      multispotSimulationPreset,
      "LM700 / Hartmann 多光斑软件联调；使用合成图和模拟参数，不代表真实计量验证完成。",
    );
    state.config.recognition.spot_count_mode = "auto";
    state.config.recognition.expected_spot_count = null;
    renderConfigFields();
    showMessage("多光斑模拟输入已填充，请确认后运行 M1。", "success");
  } catch (error) {
    showMessage(error.message);
  }
}

async function prepareStageOneFiveSpot() {
  try {
    await applyPreset(
      legacyFiveSpotPreset,
      "第一阶段五光斑兼容联调；仅用于旧接口测试，不代表真实计量验证完成。",
    );
    state.config.recognition.spot_count_mode = "fixed";
    state.config.recognition.expected_spot_count = 5;
    renderConfigFields();
    showMessage("五光斑兼容输入已填充，请确认后运行 M1。", "success");
  } catch (error) {
    showMessage(error.message);
  }
}

function validateStep(step) {
  const taskId = $("#task-id").value.trim();
  if (step >= 1 && !/^[A-Za-z0-9_-]{1,64}$/.test(taskId)) return "请输入有效任务编号。";
  if (step >= 2 && !state.calibration) return "请选择或上传标定图。";
  if (step >= 3 && !state.measurement) return "请选择或上传测量图。";
  if (step >= 4 && !state.config) return "请选择、上传或编辑配置。";
  return "";
}

function goToStep(nextStep, force = false) {
  if (nextStep < 1 || nextStep > 6) return;
  if (!force && nextStep > state.step) {
    const error = validateStep(nextStep - 1);
    if (error) return showMessage(error);
  }
  hideMessage();
  state.step = nextStep;
  $$(".step-panel").forEach((panel) => {
    const active = Number(panel.dataset.step) === nextStep;
    panel.hidden = !active;
    panel.classList.toggle("active", active);
  });
  $$(".step-button").forEach((button) => {
    const step = Number(button.dataset.step);
    button.classList.toggle("active", step === nextStep);
    button.classList.toggle("complete", step < nextStep && !validateStep(step));
    button.setAttribute("aria-current", step === nextStep ? "step" : "false");
  });
  $("#progress-label").textContent = `${nextStep} / 6`;
  $("#previous-step").disabled = nextStep === 1;
  $("#next-step").hidden = nextStep >= 5;
  if (nextStep === 5) renderReview();
  updateMirror();
  window.scrollTo({ top: 0, behavior: "smooth" });
}

function selectPath(kind, value) {
  state[kind] = value;
  $(`#${kind}-current`).textContent = value || "尚未选择";
  updateMirror();
}

async function uploadFile(kind, file) {
  if (!file) return;
  const taskId = $("#task-id").value.trim();
  if (!/^[A-Za-z0-9_-]{1,64}$/.test(taskId)) return showMessage("请先填写有效任务编号。 ");
  hideMessage();
  showMessage(`正在上传 ${file.name}…`, "working");
  try {
    const query = new URLSearchParams({ kind, task_id: taskId, filename: file.name });
    const response = await api(`/api/upload?${query}`, { method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: file });
    if (kind === "config") {
      await loadConfig(response.path);
      addOption("#config-existing", response.path);
    } else {
      selectPath(kind, response.path);
      addOption(`#${kind}-existing`, response.path);
    }
    hideMessage();
  } catch (error) {
    showMessage(error.message);
  } finally {
    $(`#${kind}-upload`).value = "";
  }
}

function addOption(selector, value) {
  const select = $(selector);
  if (![...select.options].some((option) => option.value === value)) select.add(new Option(value, value));
  select.value = value;
}

async function loadConfig(path) {
  if (!path) return;
  try {
    state.config = await readConfig(path);
    state.configPath = path;
    renderConfigFields();
    renderValidationState();
    updateMirror();
    hideMessage();
  } catch (error) {
    showMessage(error.message);
  }
}

function renderConfigFields() {
  const host = $("#config-fields");
  host.replaceChildren();
  Object.entries(state.config || {}).forEach(([section, values]) => {
    if (HIDDEN_CONFIG_SECTIONS.has(section)) return;
    if (typeof values !== "object" || values === null || Array.isArray(values)) return;
    const visibleEntries = Object.entries(values).filter(([key]) => !FIXED_CONFIG_FIELDS.has(`${section}.${key}`));
    if (!visibleEntries.length) return;
    const group = document.createElement("section");
    group.className = "config-group";
    group.innerHTML = `<h2>${configLabels[section] || section}</h2><div class="config-group-grid"></div>`;
    const grid = group.querySelector("div");
    visibleEntries.forEach(([key, value]) => {
      const label = document.createElement("label");
      label.className = "field";
      const metadata = sectionFieldLabels[`${section}.${key}`] || configFieldLabels[key] || { label: key, hint: "原始配置字段" };
      const input = document.createElement(metadata.options || value === true || value === false ? "select" : "input");
      input.dataset.section = section;
      input.dataset.key = key;
      if (input.tagName === "SELECT") {
        const options = metadata.options || { true: "开启", false: "关闭" };
        Object.entries(options).forEach(([optionValue, optionLabel]) => input.add(new Option(optionLabel, optionValue)));
        input.value = String(value);
      } else {
        input.type = typeof value === "number" || value === null ? "number" : "text";
        input.step = "any";
        input.value = Array.isArray(value) ? value.join(", ") : value ?? "";
        if (value === null) input.placeholder = "待确认";
      }
      input.addEventListener("change", updateConfigValue);
      const head = document.createElement("div");
      head.className = "config-field-head";
      head.append(Object.assign(document.createElement("span"), { textContent: metadata.label }));
      head.append(Object.assign(document.createElement("code"), { textContent: key }));
      label.append(head, input);
      if (metadata.hint || metadata.unit) {
        const hint = document.createElement("small");
        hint.textContent = [metadata.hint, metadata.unit].filter(Boolean).join(" · ");
        label.append(hint);
      }
      grid.append(label);
    });
    host.append(group);
  });
}

function updateConfigValue(event) {
  const { section, key } = event.target.dataset;
  const original = state.config[section][key];
  let value = event.target.value;
  if (typeof original === "boolean") value = value === "true";
  else if (Array.isArray(original)) value = value.split(",").map((item) => Number(item.trim())).filter(Number.isFinite);
  else if (typeof original === "number" || original === null) value = value === "" ? null : Number(value);
  state.config[section][key] = value;
  renderValidationState();
  updateMirror();
}

function renderReview() {
  const rows = [
    ["任务编号", $("#task-id").value.trim()],
    ["标定图", state.calibration],
    ["测量图", state.measurement],
    ["配置", state.configPath || "页面编辑配置"],
    ["运行方式", "local_image"],
  ];
  $("#review-list").innerHTML = rows.map(([label, value]) => `<div class="review-row"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong><i class="check">已就绪</i></div>`).join("");
}

async function runM1() {
  const error = validateStep(4);
  if (error) return showMessage(error);
  const button = $("#run-m1");
  button.disabled = true;
  button.textContent = "正在运行…";
  hideMessage();
  try {
    const payload = {
      task_id: $("#task-id").value.trim(),
      operator: $("#operator").value.trim(),
      notes: $("#notes").value.trim(),
      calibration_image: state.calibration,
      measurement_image: state.measurement,
      config_data: state.config,
    };
    state.result = await api("/api/run", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) });
    renderResult(state.result);
    goToStep(6);
  } catch (runError) {
    showMessage(runError.message);
  } finally {
    button.disabled = false;
    button.textContent = "运行 M1";
  }
}

function renderResult(response) {
  const result = response?.result || {};
  const status = result.status || "error";
  $("#result-status").className = `result-status ${status}`;
  $("#result-status").textContent = status === "ok" ? "运行成功" : "运行失败";
  $("#result-json").textContent = JSON.stringify(result, null, 2);
  $("#log-json").textContent = JSON.stringify(response?.log || {}, null, 2);
  if (status === "ok") {
    const warnings = result.quality?.warnings || [];
    $("#result-summary").innerHTML = warnings.length
      ? `<div class="summary-line warning">任务已生成，包含 ${warnings.length} 条待确认配置警告。</div>`
      : '<div class="summary-line">路径和配置检查通过，输入包可供 M2 使用。</div>';
  } else {
    $("#result-summary").innerHTML = `<div class="summary-line error">${escapeHtml(result.error?.message || "运行失败。")}</div>`;
  }
  renderValidationState();
  updateBundleActions(response);
  selectTab("result");
}

function setBadge(selector, label, stateName) {
  const badge = $(selector);
  badge.textContent = label;
  badge.className = `validation-badge ${stateName}`;
}

function renderValidationState() {
  const profile = state.config?.data_profile || {};
  const source = profile.data_source || "legacy";
  const validation = profile.validation_status || "undeclared";
  const confirmed = profile.hardware_parameters_confirmed === true;
  const metrologyValidated = profile.metrology_validated === true;
  const usableFor = profile.usable_for || "undeclared";
  setBadge("#data-source-badge", `数据来源 · ${source}`, source);
  setBadge("#validation-status-badge", `验证状态 · ${validation}`, validation);
  setBadge(
    "#hardware-status-badge",
    confirmed ? "硬件参数 · 已确认" : "硬件参数 · 待确认",
    confirmed ? "confirmed" : "pending",
  );
  setBadge(
    "#metrology-status-badge",
    `计量验证 · ${metrologyValidated ? "已完成" : "未完成"}`,
    metrologyValidated ? "confirmed" : "pending",
  );
  setBadge("#usable-for-badge", `可用于 · ${usableFor}`, usableFor);
}

function bundleTaskId(response = state.result) {
  const taskId = response?.result?.task_id || $("#task-id")?.value.trim();
  return /^[A-Za-z0-9_-]{1,64}$/.test(taskId || "") ? taskId : "";
}

function bundleFilename(taskId) {
  return `m1_${taskId}_m2_integration_bundle.zip`;
}

function bundleNote(taskId) {
  return [
    `M1→M2 联调包：${bundleFilename(taskId)}`,
    "请解压后将解压目录作为 project_root，",
    "读取根目录 input_package.json。",
    "包内文件已经过路径和配置检查，仅用于软件联调，",
    "metrology_validated=false，usable_for=software_integration。",
    "4.8 μm 和 1.03391 mm 均为临时参数，不得用于精度结论。",
  ].join("\n");
}

function updateBundleActions(response) {
  const result = response?.result || {};
  const taskId = bundleTaskId(response);
  const ready = result.status === "ok" && Boolean(taskId);
  const summary = $("#bundle-summary");
  const download = $("#download-bundle");
  const copy = $("#copy-bundle-note");
  summary.hidden = !response;
  download.disabled = !ready;
  copy.disabled = !ready;
  $("#bundle-status").textContent = ready
    ? `可下载：${bundleFilename(taskId)}`
    : "当前结果不可生成联调包";
}

async function downloadBundle() {
  const taskId = bundleTaskId();
  if (!taskId) return showMessage("当前没有可下载的成功任务。");
  const button = $("#download-bundle");
  button.disabled = true;
  try {
    const response = await fetch(`/api/task/${encodeURIComponent(taskId)}/bundle`);
    if (!response.ok) {
      const data = await response.json().catch(() => ({}));
      throw new Error(data.error?.message || `联调包下载失败（${response.status}）。`);
    }
    const blob = await response.blob();
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = bundleFilename(taskId);
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    showMessage("完整 M1 → M2 联调包已开始下载。", "success");
  } catch (error) {
    showMessage(error.message);
  } finally {
    button.disabled = false;
  }
}

async function copyBundleNote() {
  const taskId = bundleTaskId();
  if (!taskId) return showMessage("当前没有可复制说明的成功任务。");
  const note = bundleNote(taskId);
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(note);
    } else {
      const textarea = document.createElement("textarea");
      textarea.value = note;
      textarea.setAttribute("readonly", "");
      textarea.style.position = "fixed";
      textarea.style.opacity = "0";
      document.body.append(textarea);
      textarea.select();
      if (!document.execCommand("copy")) throw new Error("浏览器拒绝了复制操作。");
      textarea.remove();
    }
    showMessage("联调说明已复制，可直接发送给 M2。", "success");
  } catch (error) {
    showMessage(error.message || "复制失败，请手动复制页面中的文件名。");
  }
}

function selectTab(tab) {
  $$(".tab").forEach((button) => button.classList.toggle("active", button.dataset.tab === tab));
  $("#result-json").hidden = tab !== "result";
  $("#log-json").hidden = tab !== "log";
}

function updateMirror() {
  const taskId = $("#task-id")?.value.trim() || "未命名";
  $("#mirror-task").textContent = taskId;
  $("#mirror-calibration").textContent = state.calibration || "未选择";
  $("#mirror-measurement").textContent = state.measurement || "未选择";
  $("#mirror-config").textContent = state.configPath || (state.config ? "页面编辑配置" : "未选择");
  const ready = [taskId !== "未命名" && /^[A-Za-z0-9_-]{1,64}$/.test(taskId), state.calibration, state.measurement, state.config].filter(Boolean).length;
  $("#readiness-value").textContent = `${ready} / 4`;
  $("#readiness-bar").style.width = `${ready * 25}%`;
}

function renderRecentTasks() {
  const host = $("#recent-tasks");
  const tasks = state.bootstrap?.recent_tasks || [];
  if (!tasks.length) return;
  host.replaceChildren(...tasks.map((task) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = task.task_id;
    button.addEventListener("click", async () => {
      try {
        const response = await api(`/api/task/${encodeURIComponent(task.task_id)}`);
        state.result = response;
        $("#task-id").value = task.task_id;
        renderResult(response);
        goToStep(6, true);
      } catch (error) { showMessage(error.message); }
    });
    return button;
  }));
}

function resetTask() {
  state.calibration = "";
  state.measurement = "";
  state.config = structuredClone(state.bootstrap.default_config);
  state.configPath = "config/default_config.json";
  state.result = null;
  $("#task-id").value = createTaskId();
  $("#operator").value = "";
  $("#notes").value = "";
  $("#calibration-existing").value = "";
  $("#measurement-existing").value = "";
  $("#config-existing").value = state.configPath;
  $("#calibration-current").textContent = "尚未选择";
  $("#measurement-current").textContent = "尚未选择";
  updateBundleActions(null);
  renderConfigFields();
  renderValidationState();
  goToStep(1);
}

function showMessage(text, kind = "error") {
  const message = $("#message");
  message.textContent = text;
  message.dataset.kind = kind;
  message.hidden = false;
}

function hideMessage() { $("#message").hidden = true; }

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>'"]/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" }[char]));
}

document.addEventListener("DOMContentLoaded", initialize);
