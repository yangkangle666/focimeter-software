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

const configLabels = {
  camera: "相机",
  optical: "光学",
  image_processing: "图像处理",
  recognition: "光斑识别",
  calculation: "计算单位",
  path_policy: "路径策略",
};

const configFieldLabels = {
  pixel_size_um: { label: "像素尺寸", hint: "相机单个像素的物理尺寸", unit: "μm" },
  image_width: { label: "图像宽度", hint: "图像横向像素数，待确认", unit: "pixel" },
  image_height: { label: "图像高度", hint: "图像纵向像素数，待确认", unit: "pixel" },
  distance_m: { label: "光学传播距离", hint: "Hartmann 阵列到传感器的距离", unit: "m" },
  hartmann_spacing_mm: { label: "Hartmann 点阵间距", hint: "点阵间距，待确认", unit: "mm" },
  roi_width_ratio: { label: "ROI 宽度比例", hint: "图像横向保留区域", unit: "比例" },
  roi_height_ratio: { label: "ROI 高度比例", hint: "图像纵向保留区域", unit: "比例" },
  median_kernel: { label: "中值滤波核", hint: "必须为正奇数", unit: "尺寸" },
  tophat_kernel: { label: "顶帽滤波核", hint: "用于增强光斑区域", unit: "尺寸" },
  otsu_a: { label: "Otsu 下阈值", hint: "归一化阈值下界", unit: "0–1" },
  otsu_b: { label: "Otsu 上阈值", hint: "归一化阈值上界", unit: "0–1" },
  max_depth: { label: "最大处理深度", hint: "图像处理允许的深度", unit: "层" },
  expected_spot_count: { label: "期望光斑数量", hint: "标准点阵中的光斑数量", unit: "个" },
  min_confidence: { label: "最低识别置信度", hint: "低于此值的识别结果会被关注", unit: "0–1" },
  pixel_threshold: { label: "像素位移阈值", hint: "允许的最小像素变化", unit: "pixel" },
  angle_unit: { label: "角度单位", hint: "接口固定使用 degree", options: { degree: "degree · 度" } },
  diopter_unit: { label: "屈光度单位", hint: "接口固定使用 D", options: { D: "D · 屈光度" } },
  path_type: { label: "路径类型", hint: "所有输入路径相对于项目根目录", options: { relative_to_project_root: "项目根目录相对路径" } },
  allow_absolute_path: { label: "允许绝对路径", hint: "为保护项目边界，必须关闭" },
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
    renderConfigFields();
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
  $("#download-bundle").addEventListener("click", downloadBundle);
  $("#copy-bundle-note").addEventListener("click", copyBundleNote);
  $$(".tab").forEach((tab) => tab.addEventListener("click", () => selectTab(tab.dataset.tab)));
}

function populateSelect(selector, values, placeholder) {
  const select = $(selector);
  select.replaceChildren(new Option(placeholder, ""), ...values.map((value) => new Option(value, value)));
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
    const response = await fetch(`/api/file?${new URLSearchParams({ path })}`);
    if (!response.ok) throw new Error("无法读取配置文件。");
    state.config = await response.json();
    state.configPath = path;
    renderConfigFields();
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
    if (typeof values !== "object" || values === null || Array.isArray(values)) return;
    const group = document.createElement("section");
    group.className = "config-group";
    group.innerHTML = `<h2>${configLabels[section] || section}</h2><div class="config-group-grid"></div>`;
    const grid = group.querySelector("div");
    Object.entries(values).forEach(([key, value]) => {
      const label = document.createElement("label");
      label.className = "field";
      const metadata = configFieldLabels[key] || { label: key, hint: "原始配置字段" };
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
        input.value = value ?? "";
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
  else if (typeof original === "number" || original === null) value = value === "" ? null : Number(value);
  state.config[section][key] = value;
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
  updateBundleActions(response);
  selectTab("result");
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
    "不代表真实计量验证完成。",
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
