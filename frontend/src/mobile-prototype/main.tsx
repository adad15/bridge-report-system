import { useEffect, useMemo, useRef, useState, type ChangeEvent } from "react";
import ReactDOM from "react-dom/client";
import { Alert, Button, ConfigProvider, Drawer, Empty, Input, InputNumber, Progress, Select, Tabs, Tag } from "antd";
import zhCN from "antd/locale/zh_CN";
import { ArrowLeftOutlined, CameraOutlined, CheckCircleFilled, CheckOutlined, CloudUploadOutlined, CloseOutlined, DeleteOutlined, DownOutlined, HomeOutlined, MoreOutlined, PlusOutlined, SearchOutlined, RightOutlined, SafetyCertificateFilled, SyncOutlined, TeamOutlined, UnorderedListOutlined } from "@ant-design/icons";
import { blankTask, bridgeName, componentCanComplete, componentStatus, components, descriptionFor, displayGroup, indicatorById, indicatorsFor, initialMeasurements, inspectionYear, inspectors, ownerByComponent, inspectorName, type Component, type Measurement, type NewDefect, type Review, type TaskState } from "./model";
import { clearTask, imageToDataUrl, loadTask, saveTask } from "./store";
import "./style.css";
import "./style-modern.css";
import "./style-reference.css";

type Screen = "home" | "components" | "component" | "review" | "chooser" | "form" | "team" | "upload" | "score";
type FormDraft = Omit<NewDefect, "id" | "createdAt">;
const primaryGroups: Component["primary"][] = ["上部结构", "下部结构", "桥面系"];
const emptyForm = (componentId: string): FormDraft => ({
  componentId, indicatorId: "", location: "", direction: "",
  measurements: [], photos: [], photoReason: "", note: "",
});
function splitComponentName(name: string) {
  const match = /^(\S+#)\s*(.*)$/.exec(name);
  return match ? { number: match[1], label: match[2] } : { number: name, label: "" };
}
function App() {
  const [task, setTask] = useState<TaskState>(blankTask);
  const [ready, setReady] = useState(false);
  const [saveLabel, setSaveLabel] = useState("正在读取本地记录");
  const [screen, setScreen] = useState<Screen>("home");
  const [activeInspectorId, setActiveInspectorId] = useState("zhang");
  const [online, setOnline] = useState(false);
  const [primary, setPrimary] = useState<Component["primary"]>("上部结构");
  const [secondary, setSecondary] = useState("上部承重构件");
  const [componentId, setComponentId] = useState(components[0].id);
  const [selectedId, setSelectedId] = useState("");
  const [search, setSearch] = useState("");
  const [componentSearch, setComponentSearch] = useState("");
  const [showComponentSearch, setShowComponentSearch] = useState(false);
  const [groupOpen, setGroupOpen] = useState(false);
  const [demoOpen, setDemoOpen] = useState(false);
  const [groupFilterActive, setGroupFilterActive] = useState(false);
  const [pickerTab, setPickerTab] = useState("all");
  const [form, setForm] = useState<FormDraft>(emptyForm(components[0].id));
  const [editingId, setEditingId] = useState("");
  const [reviewId, setReviewId] = useState("");
  const [reviewDraft, setReviewDraft] = useState<Review>({ change: "", repair: "", observation: "", confirmedAt: "" });
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const bodyRef = useRef<HTMLDivElement>(null);
  const photoInputRef = useRef<HTMLInputElement>(null);
  const component = components.find((item) => item.id === componentId) ?? components[0];
  const locked = false;
  const indicator = indicatorById(form.indicatorId);
  const available = useMemo(() => indicatorsFor(component), [component]);
  const localDefects = task.defects.filter((item) => item.componentId === component.id);
  const ownerId = ownerByComponent[component.id];
  const isOwner = ownerId === activeInspectorId;
  const sharedSnapshot = task.syncedByInspector[activeInspectorId]?.[component.id];

  const visibleDefects = isOwner ? localDefects : sharedSnapshot?.defects ?? [];
  const visibleReview = (historyId: string) => isOwner ? task.reviews[historyId] : sharedSnapshot?.reviews[historyId];

  const myComponents = components.filter((item) => ownerByComponent[item.id] === activeInspectorId);
  const priorityComponent = myComponents.find((item) => !task.completed[item.id]) ?? myComponents[0];
  const priorityIdentity = splitComponentName(priorityComponent.name);
  const myCompleted = myComponents.every((item) => task.completed[item.id]);
  const myNeedsSubmit = myComponents.some((item) => !task.submissions[item.id] || task.dirty[item.id]);
  const canSubmitMine = online && myCompleted && myNeedsSubmit && !busy;
  const submittedCount = components.filter((item) => Boolean(task.submissions[item.id])).length;
  const syncedCount = components.filter((item) => ownerByComponent[item.id] !== activeInspectorId && Boolean(task.syncedByInspector[activeInspectorId]?.[item.id])).length;
  const pendingRemoteCount = components.filter((item) => ownerByComponent[item.id] !== activeInspectorId && Boolean(task.submissions[item.id]) && (task.syncedByInspector[activeInspectorId]?.[item.id]?.revision ?? 0) < task.submissions[item.id].revision).length;
  const scoreReady = submittedCount === components.length;
  const hasLocalChanges = myComponents.some((item) => task.dirty[item.id]);
  const pendingHistory = component.history.filter((item) => !visibleReview(item.id));
  const diseaseRows = [
    ...component.history.map((history) => ({
      kind: "history" as const, id: history.id, name: history.indicatorName,
      location: history.location, year: history.year,
      summary: visibleReview(history.id)
        ? "本次：" + visibleReview(history.id)?.change + " · " + visibleReview(history.id)?.repair
        : "上次：" + history.description,
      state: visibleReview(history.id) ? "已复查" : "待复查",
      pending: !visibleReview(history.id),
    })),
    ...visibleDefects.map((record) => ({
      kind: "new" as const, id: record.id,
      name: indicatorById(record.indicatorId)?.name ?? "病害指标",
      location: record.location, year: inspectionYear,
      summary: descriptionFor(record), state: "本次新增", pending: false,
    })),
  ].sort((a, b) => Number(b.pending) - Number(a.pending));

  useEffect(() => {
    loadTask().then((loaded) => {
      setTask(loaded);
      setSaveLabel("已从本机读取");
      setReady(true);
    }).catch(() => {
      setSaveLabel("本地存储不可用");
      setReady(true);
    });
  }, []);

  async function commit(next: TaskState): Promise<boolean> {
    setBusy(true);
    setError("");
    setSaveLabel("正在保存到本机");
    try {
      await saveTask(next);
      setTask(next);
      setSaveLabel("已保存到本机");
      return true;
    } catch {
      setError("本地保存失败，请保留页面并重试。");
      setSaveLabel("保存失败");
      return false;
    } finally {
      setBusy(false);
    }
  }

  function go(next: Screen) {
    setError("");
    setGroupOpen(false);
    setScreen(next);
    bodyRef.current?.scrollTo({ top: 0, behavior: "instant" });
  }

  function back() {
    if (groupOpen) { setGroupOpen(false); return; }
    if (screen === "form") go(editingId ? "component" : "chooser");
    else if (screen === "chooser" || screen === "review") go("component");
    else if (screen === "component") go("components");
    else if (screen === "upload") go("team");
    else go("home");
  }

  function openComponent(item: Component) {
    setComponentId(item.id);
    setPrimary(item.primary);
    setSecondary(item.secondary);
    go("component");
  }

  function startNew() {
    setSelectedId("");
    setSearch("");
    setPickerTab("all");
    setForm(emptyForm(component.id));
    setEditingId("");
    go("chooser");
  }

  function nextToForm() {
    const item = available.find((entry) => entry.id === selectedId);
    if (!item) {
      setError("请先选择一个适用于当前构件的病害指标。");
      return;
    }
    setForm((current) => ({
      ...current, componentId: component.id, indicatorId: item.id,
      measurements: current.indicatorId === item.id ? current.measurements : initialMeasurements(item),
    }));
    go("form");
  }

  function editDefect(record: NewDefect) {
    setEditingId(record.id);
    setSelectedId(record.indicatorId);
    setForm({
      componentId: record.componentId, indicatorId: record.indicatorId,
      location: record.location, direction: record.direction,
      measurements: record.measurements, photos: record.photos,
      photoReason: record.photoReason, note: record.note,
    });
    go("form");
  }

  function updateMeasurement(index: number, change: Partial<Measurement>) {
    setForm((current) => ({
      ...current,
      measurements: current.measurements.map((item, position) =>
        position === index ? { ...item, ...change } : item
      ),
    }));
  }

  async function onPhotos(event: ChangeEvent<HTMLInputElement>) {
    const files = Array.from(event.target.files ?? []).slice(0, Math.max(0, 5 - form.photos.length));
    if (!files.length) return;
    try {
      const photos = await Promise.all(files.map(async (file) => ({
        id: crypto.randomUUID(), name: file.name, dataUrl: await imageToDataUrl(file),
      })));
      setForm((current) => ({ ...current, photos: [...current.photos, ...photos] }));
      setError("");
    } catch {
      setError("照片读取失败，请重新选择图片。");
    }
    event.target.value = "";
  }

  function formError(): string {
    if (!indicator || !available.some((item) => item.id === indicator.id)) return "请选择适用的病害指标。";
    if (!form.location.trim()) return "请填写具体病害位置。";
    if (!form.measurements.length) return "请填写本次测量。";
    for (const item of form.measurements) {
      if (!item.value || !Number.isFinite(Number(item.value)) || Number(item.value) <= 0) return "请填写大于 0 的本次测量值。";
      if (item.end && (!Number.isFinite(Number(item.end)) || Number(item.end) < Number(item.value))) return "范围终值不能小于起值。";
    }
    if (!form.photos.length && !form.photoReason.trim()) return "请添加本次照片，或填写暂缺照片原因。";
    return "";
  }

  async function saveDefect() {
    if (!isOwner || locked) return;
    const problem = formError();
    if (problem) { setError(problem); return; }
    const now = new Date().toISOString();
    const record: NewDefect = {
      ...form, id: editingId || crypto.randomUUID(),
      location: form.location.trim(), note: form.note.trim(),
      photoReason: form.photoReason.trim(),
      createdAt: editingId ? task.defects.find((item) => item.id === editingId)?.createdAt ?? now : now,
    };
    const next: TaskState = {
      ...task,
      defects: editingId ? task.defects.map((item) => item.id === editingId ? record : item) : [...task.defects, record],
      dirty: { ...task.dirty, [component.id]: true },
      newChecked: { ...task.newChecked, [component.id]: false },
      completed: { ...task.completed, [component.id]: false },
    };
    if (await commit(next)) go("component");
  }

  async function deleteDefect(record: NewDefect): Promise<boolean> {
    if (!isOwner || locked) return false;
    if (!window.confirm("移除这条保存在本机的新增病害？")) return false;
    const next: TaskState = {
      ...task,
      defects: task.defects.filter((item) => item.id !== record.id),
      dirty: { ...task.dirty, [component.id]: true },
      newChecked: { ...task.newChecked, [component.id]: false },
      completed: { ...task.completed, [component.id]: false },
    };
    return commit(next);
  }

  function openReview(historyId: string) {
    setReviewId(historyId);
    setReviewDraft(visibleReview(historyId) ?? { change: "", repair: "", observation: "", confirmedAt: "" });
    go("review");
  }

  async function saveReview() {
    if (!isOwner || locked) return;
    if (!reviewDraft.change || !reviewDraft.repair) { setError("请分别确认病害变化和维修情况。"); return; }
    if (["本次未见", "无法判定"].includes(reviewDraft.change) && !reviewDraft.observation.trim()) {
      setError("请填写本次观察情况或无法判定的原因。"); return;
    }
    const next: TaskState = {
      ...task,
      reviews: { ...task.reviews, [reviewId]: { ...reviewDraft, observation: reviewDraft.observation.trim(), confirmedAt: new Date().toISOString() } },
      dirty: { ...task.dirty, [component.id]: true },
      completed: { ...task.completed, [component.id]: false },
    };
    if (await commit(next)) go("component");
  }

  async function completeComponent() {
    if (!isOwner || locked) return;
    if (!componentCanComplete(task, component)) {
      setError("请先逐条复查历史病害。"); return;
    }
    const alreadyComplete = Boolean(task.completed[component.id] && task.newChecked[component.id]);
    const next: TaskState = alreadyComplete ? task : {
      ...task,
      newChecked: { ...task.newChecked, [component.id]: true },
      completed: { ...task.completed, [component.id]: true },
      dirty: { ...task.dirty, [component.id]: true },
    };
    if (next === task || await commit(next)) {
      const following = components.find((item) => ownerByComponent[item.id] === activeInspectorId && !next.completed[item.id]);
      if (following) openComponent(following);
      else go("team");
    }
  }

  async function submitMine() {
    if (!online) { setError("当前离线，记录仍保存在本机。联网后由你主动提交。"); return; }
    if (!myCompleted) { setError("请先完成自己负责的全部构件。"); return; }
    if (!myNeedsSubmit) { setError("你负责的构件已提交，没有新的本机修改。"); return; }
    setBusy(true);
    await new Promise((resolve) => window.setTimeout(resolve, 450));
    setBusy(false);
    const submittedAt = new Date().toISOString();
    const submissions = { ...task.submissions };
    const dirty = { ...task.dirty };
    myComponents.forEach((item) => {
      if (submissions[item.id] && !dirty[item.id]) return;
      submissions[item.id] = {
        inspectorId: activeInspectorId,
        submittedAt,
        revision: (submissions[item.id]?.revision ?? 0) + 1,
        defects: task.defects.filter((record) => record.componentId === item.id),
        reviews: Object.fromEntries(item.history.filter((history) => task.reviews[history.id]).map((history) => [history.id, task.reviews[history.id]])),
        newChecked: Boolean(task.newChecked[item.id]),
      };
      dirty[item.id] = false;
    });
    if (await commit({ ...task, submissions, dirty })) setSaveLabel("我的构件已提交（演示）");
  }

  async function syncTeam() {
    if (!online) { setError("当前离线，可查看上次同步的记录；联网后再手动同步。"); return; }
    setBusy(true);
    await new Promise((resolve) => window.setTimeout(resolve, 420));
    setBusy(false);
    const current = { ...(task.syncedByInspector[activeInspectorId] ?? {}) };
    components.forEach((item) => {
      if (ownerByComponent[item.id] === activeInspectorId) return;
      const submission = task.submissions[item.id];
      if (submission) current[item.id] = structuredClone(submission);
    });
    const next: TaskState = {
      ...task,
      syncedByInspector: { ...task.syncedByInspector, [activeInspectorId]: current },
      lastSyncedAt: { ...task.lastSyncedAt, [activeInspectorId]: new Date().toISOString() },
    };
    if (await commit(next)) setSaveLabel("队友记录已同步到本机");
  }

  async function resetDemo() {
    if (!window.confirm("清空此浏览器中的原型演示记录？")) return;
    try {
      await clearTask();
      setTask(blankTask());
      setActiveInspectorId("zhang");
      setOnline(false);
      setGroupFilterActive(false);
      setDemoOpen(false);
      setComponentSearch("");
      setShowComponentSearch(false);
      setSaveLabel("演示数据已重置");
      setComponentId(components[0].id);
      setPrimary("上部结构");
      setSecondary("上部承重构件");
      go("home");
    } catch { setError("清空本地记录失败。"); }
  }

  async function fillDemo() {
    const reviews: Record<string, Review> = {};
    components.forEach((item) => item.history.forEach((history) => {
      reviews[history.id] = { change: "无明显变化", repair: "未见维修", observation: "现场已复查。", confirmedAt: new Date().toISOString() };
    }));
    const submittedAt = new Date().toISOString();
    const submissions = Object.fromEntries(components.filter((item) => ownerByComponent[item.id] !== "zhang").map((item) => [
      item.id,
      {
        inspectorId: ownerByComponent[item.id],
        submittedAt,
        revision: 1,
        defects: task.defects.filter((record) => record.componentId === item.id),
        reviews: Object.fromEntries(item.history.map((history) => [history.id, reviews[history.id]])),
        newChecked: true,
      },
    ]));
    const next: TaskState = {
      ...task,
      reviews,
      newChecked: Object.fromEntries(components.map((item) => [item.id, true])),
      completed: Object.fromEntries(components.map((item) => [item.id, true])),
      submissions,
      syncedByInspector: {},
      lastSyncedAt: {},
      dirty: {},
    };
    if (await commit(next)) { setSaveLabel("多人示例已准备"); setDemoOpen(false); }
  }

  const headings: Record<Screen, string> = {
    home: "本桥检测任务", components: "病害清单", component: component.name,
    team: "团队作业",
    review: "复查既有病害", chooser: "新增病害", form: "填写病害信息",
    upload: "提交与同步", score: "桥梁算分",
  };
  const currentGroup = components.filter((item) => item.primary === primary);
  const secondaryGroups = Array.from(new Set(currentGroup.map((item) => item.secondary)));
  const shownComponents = components.filter((item) =>
    (!groupFilterActive || (item.primary === primary && item.secondary === secondary)) &&
    (!componentSearch.trim() || item.name.includes(componentSearch.trim()))
  ).sort((left, right) => {
    const visibleCount = (item: Component) => item.history.length + ((ownerByComponent[item.id] === activeInspectorId || task.syncedByInspector[activeInspectorId]?.[item.id]) ? (ownerByComponent[item.id] === activeInspectorId ? task.defects.filter((record) => record.componentId === item.id).length : task.syncedByInspector[activeInspectorId]?.[item.id]?.defects.length ?? 0) : 0);
    return visibleCount(right) - visibleCount(left);
  });
  const visibleHistoryCount = shownComponents.reduce((count, item) => count + item.history.length, 0);
  const filteredIndicators = available.filter((item) => {
    const query = search.trim().toLowerCase();
    if (query && !(item.name.toLowerCase().includes(query) || item.number.toLowerCase().includes(query))) return false;
    if (pickerTab === "common") {
      const common = component.catalogNumber === "5.1.1" ? ["5.1.1-2", "5.1.1-5", "5.1.1-11"] : available.slice(0, 3).map((entry) => entry.number);
      return common.includes(item.number);
    }
    if (pickerTab === "recent") return task.defects.some((record) => record.indicatorId === item.id);
    return true;
  });
  const indicatorSections = Array.from(new Set(filteredIndicators.map(displayGroup)));


  return (
    <div className="prototype-stage">
      <aside className="prototype-guide">
        <div className="guide-mark"><SafetyCertificateFilled /> 桥梁现场检测</div>
        <h1>2026 年度检查<br />交互原型</h1>
        <p>按构件完成复查，新增病害直接绑定年度评定树指标。现场记录先留在本机；每位队员联网后提交自己负责的构件，其他队员再按需同步查看。</p>
        <div className="guide-steps">
          <span>01 · 选择构件组和构件</span>
          <span>02 · 对照历史并新增病害</span>
          <span>03 · 各自提交负责的构件</span>
          <span>04 · 同步队友记录，资料齐全后算分</span>
        </div>
        <div className="guide-actions">
          <Button onClick={() => { void fillDemo(); }}>填充多人提交示例</Button>
          <Button onClick={() => { void resetDemo(); }}>重置本地演示</Button>
        </div>
        <div className="demo-controls">
          <span>演示身份</span>
          <div>{inspectors.map((person) => <button key={person.id} className={activeInspectorId === person.id ? "active" : ""} onClick={() => setActiveInspectorId(person.id)}>{person.name}</button>)}</div>
          <span>演示网络</span>
          <button className={online ? "network-on" : "network-off"} onClick={() => setOnline(!online)}>{online ? "在线 · 点击切为离线" : "离线 · 点击恢复网络"}</button>
        </div>
        <small>原型使用真实评定树病害目录；跨设备提交、同步和服务器算分为模拟交互。</small>
      </aside>

      <div className="phone-frame">
        <div className="phone-status"><span>9:41</span><span className="status-icons">●●●  ▰</span></div>
        <header className={"phone-header " + (screen === "components" ? "components-header" : "")}>
          {screen !== "home" ? <Button type="text" aria-label="返回" icon={<ArrowLeftOutlined />} onClick={back} /> : <span className="header-brand"><SafetyCertificateFilled /></span>}
          <div className="header-title"><strong>{headings[screen]}</strong><small>{screen === "components" ? inspectionYear + " · " + visibleHistoryCount + " 条既有病害" : screen === "home" ? bridgeName + " · " + inspectionYear : component && ["component", "review", "chooser", "form"].includes(screen) ? component.name : bridgeName + " · " + inspectionYear}</small></div>
          {screen === "components" ? <nav className="header-list-actions" aria-label="构件清单操作">
            <button type="button" className="header-group-switch" aria-label={"选择构件组，当前 " + (groupFilterActive ? secondary : "全桥构件")} aria-haspopup="dialog" aria-expanded={groupOpen} onClick={() => setGroupOpen(true)}><span>{groupFilterActive ? secondary : "全桥"}</span><small>{shownComponents.length}件</small><DownOutlined /></button>
            <button type="button" className={"header-search-toggle " + (showComponentSearch ? "is-active" : "")} aria-label={showComponentSearch ? "收起搜索" : "搜索构件"} aria-pressed={showComponentSearch} onClick={() => { if (showComponentSearch) setComponentSearch(""); setShowComponentSearch(!showComponentSearch); }}>{showComponentSearch ? <CloseOutlined /> : <SearchOutlined />}</button>
          </nav> : <Button className="header-more" type="text" aria-label="打开原型演示设置" icon={<MoreOutlined />} onClick={() => setDemoOpen(true)} />}
        </header>
        <div className={"save-indicator " + (saveLabel.includes("失败") || saveLabel.includes("不可用") ? "is-error" : "")} role="status">
          <span><span className="save-dot" />{saveLabel}</span>
          <span className={"network-indicator " + (online ? "online" : "offline")}>{online ? "在线" : "离线"} · 已提交 {submittedCount}/{components.length}</span>
        </div>

        <div className="phone-body" ref={bodyRef}>
          {!ready ? <div className="loading-note">正在读取本机作业记录…</div> : null}

          {ready && screen === "home" && (
            <div className="screen-content home-page">
              <div className="home-greeting"><span>{inspectorName(activeInspectorId)}，继续今天的检查</span><span>{inspectionYear} 年度</span></div>
              <section className="hero-card">
                <div className="hero-top"><span className="hero-kicker">年度定期检测 · 现场任务</span><span className="hero-badge">进行中</span></div>
                <h2>{bridgeName}</h2>
                <p>梁式桥 · 5 件示例构件 · 评定树 2.0.3</p>
                <div className="hero-divider" />
                <div className="hero-progress"><span>全桥已提交</span><strong>{submittedCount}<em> / {components.length}</em></strong></div>
                <Progress className="hero-progress-bar" percent={Math.round(submittedCount / components.length * 100)} showInfo={false} strokeColor="#9B3DF6" railColor="#F0E9F5" />
                <div className="hero-metrics"><span>我的完成 <b>{myComponents.filter((item) => task.completed[item.id]).length}/{myComponents.length}</b></span><span>队友已同步 <b>{syncedCount}/{components.length - myComponents.length}</b></span></div>
              </section>
              <button className="home-primary" onClick={() => go("components")}>
                <span className="home-primary-icon"><UnorderedListOutlined /></span>
                <span><strong>继续检查构件</strong><small>查看病害、记录变化和新增病害</small></span>
                <RightOutlined />
              </button>
              <div className="section-head"><h3>{myCompleted ? "最近检查" : "接下来处理"}</h3><span>{myCompleted ? "我的构件已完成" : myComponents.filter((item) => !task.completed[item.id]).length + " 件待完成"}</span></div>
              <button className="priority-card" onClick={() => openComponent(priorityComponent)}>
                <span className="priority-top"><span className="priority-identity"><strong>{priorityIdentity.number}</strong>{priorityIdentity.label ? <em>{priorityIdentity.label}</em> : null}</span><span className={task.completed[priorityComponent.id] ? "done" : ""}>{task.completed[priorityComponent.id] ? "已完成" : "待检查"}</span></span>
                <small className="priority-context">{priorityComponent.secondary} · {inspectorName(ownerByComponent[priorityComponent.id])}负责</small>
                <span className="priority-disease">{priorityComponent.history.length ? priorityComponent.history.length + " 条既有病害 · " + priorityComponent.history.map((entry) => entry.indicatorName).join("、") : "上次无病害记录"}</span>
                <small>{priorityComponent.history[0] ? "上次：" + priorityComponent.history[0].description : "本次仍需现场检查并明确结论"}</small>
                <span className="priority-action">进入构件 <RightOutlined /></span>
              </button>
              <div className="home-shortcuts">
                <button onClick={() => go("team")}><span className="shortcut-icon"><TeamOutlined /></span><strong>团队协作</strong><small>{pendingRemoteCount ? pendingRemoteCount + " 件待同步" : "查看分工与提交"}</small></button>
                <button onClick={() => go("score")}><span className="shortcut-icon score"><CheckCircleFilled /></span><strong>桥梁评分</strong><small>{scoreReady ? "查看试算结果" : "等待资料齐全"}</small></button>
              </div>
              <p className="home-footnote">现场记录保存在本机。每人提交自己负责的构件，其他队员主动同步后查看。</p>
            </div>
          )}

          {ready && screen === "components" && (
            <div className="screen-content list-page disease-index-page">
              {showComponentSearch ? <Input className="compact-component-search" size="large" autoFocus prefix={<SearchOutlined />} placeholder="搜索构件编号或名称" value={componentSearch} onChange={(event) => setComponentSearch(event.target.value)} allowClear aria-label="搜索构件" /> : null}
              <div className="component-disease-list">
                {shownComponents.map((item) => {
                  const status = componentStatus(task, item);
                  const mine = ownerByComponent[item.id] === activeInspectorId;
                  const sharedSnapshot = task.syncedByInspector[activeInspectorId]?.[item.id];
                  const shared = mine || Boolean(sharedSnapshot);
                  const added = mine ? task.defects.filter((record) => record.componentId === item.id) : sharedSnapshot?.defects ?? [];
                  const pending = item.history.filter((history) => !(mine ? task.reviews[history.id] : sharedSnapshot?.reviews[history.id])).length;
                  const previews = [
                    ...item.history.map((history) => ({
                      id: history.id,
                      name: history.indicatorName,
                      detail: [history.location, history.description].filter(Boolean).join(" · "),
                      state: !shared ? "待同步" : (mine ? task.reviews[history.id] : sharedSnapshot?.reviews[history.id]) ? "已复查" : "待复查",
                      kind: "history" as const,
                    })),
                    ...added.map((record) => ({
                      id: record.id,
                      name: indicatorById(record.indicatorId)?.name ?? "新增病害",
                      detail: [record.location, descriptionFor(record)].filter(Boolean).join(" · "),
                      state: "本次新增",
                      kind: "new" as const,
                    })),
                  ];
                  const count = previews.length;
                  const identity = splitComponentName(item.name);
                  const footerSummary = !shared
                    ? task.submissions[item.id] ? "队友记录待同步" : "等待队友提交"
                    : [
                      item.history.length ? pending ? pending + " 条待复查" : "既有病害已复查" : "",
                      added.length ? "本次新增 " + added.length + " 条" : "",
                    ].filter(Boolean).join(" · ");
                  return <button className={"component-disease-card " + (count ? "has-disease" : "no-disease")} key={item.id} onClick={() => openComponent(item)}>
                    <span className="component-disease-meta"><span className="component-identity"><strong>{identity.number}</strong>{identity.label ? <em>{identity.label}</em> : null}</span><span className={status === "已完成" && shared ? "is-done" : ""}>{mine ? status : shared ? task.submissions[item.id]?.revision > sharedSnapshot!.revision ? "有更新待同步" : "已同步" : task.submissions[item.id] ? "待同步" : "待队友提交"}</span></span>
                    <span className="component-owner">{item.secondary} · {inspectorName(ownerByComponent[item.id])}负责</span>
                    {count ? <>
                      <span className="component-disease-section"><span>病害记录</span><strong>{count} 条</strong></span>
                      <span className="component-defect-list">
                        {previews.map((entry, index) => <span className="component-defect-preview" key={entry.kind + entry.id}>
                          <span className="component-defect-order">{String(index + 1).padStart(2, "0")}</span>
                          <span className="component-defect-copy">
                            <span className="component-defect-head"><span className="component-defect-name">{entry.name}</span><span className={"component-defect-state " + (entry.kind === "new" ? "is-new" : entry.state === "待复查" ? "is-pending" : "")}>{entry.state}</span></span>
                            <small>{entry.detail}</small>
                          </span>
                        </span>)}
                      </span>
                      <span className="disease-preview-footer"><span>{footerSummary}</span><span>查看全部 <RightOutlined /></span></span>
                    </> : <>
                      <strong className="no-disease-name">上次无病害记录</strong>
                      <span className="no-disease-hint">本次仍需现场检查并明确确认</span>
                      <span className="disease-preview-footer"><span>待检查</span><span>进入构件 <RightOutlined /></span></span>
                    </>}
                  </button>;
                })}
              </div>
              {!shownComponents.length ? <Empty description="当前构件组没有匹配的构件" /> : null}
            </div>
          )}

          {ready && screen === "component" && (
            <div className="screen-content detail-page disease-first-page">
              <div className="component-switcher">
                <button disabled={components.indexOf(component) === 0} onClick={() => openComponent(components[components.indexOf(component) - 1])}><ArrowLeftOutlined /> 上一构件</button>
                <span>{component.primary} · {components.indexOf(component) + 1}/{components.length}</span>
                <button disabled={components.indexOf(component) === components.length - 1} onClick={() => openComponent(components[components.indexOf(component) + 1])}>下一构件 <RightOutlined /></button>
              </div>
              <div className="disease-detail-head">
                <div><small>2026 年度现场记录</small><h2>病害记录 <b>{diseaseRows.length}</b></h2></div>
                {!locked ? <Button className="detail-add-button" disabled={!isOwner} icon={<PlusOutlined />} onClick={startNew}>新增病害</Button> : null}
              </div>
              <div className="disease-detail-subline">
                <span>待复查 <strong>{pendingHistory.length}</strong></span>
                <span>本次新增 <strong>{visibleDefects.length}</strong></span>
                <span>{component.material}</span>
              </div>
              {diseaseRows.length ? <div className="disease-list">
                {diseaseRows.map((row) => <button className={"disease-card " + (row.pending ? "needs-review" : "")} key={row.kind + row.id} onClick={() => {
                  if (row.kind === "history") openReview(row.id);
                  else {
                    const record = visibleDefects.find((item) => item.id === row.id);
                    if (record) editDefect(record);
                  }
                }}>
                  <div className="disease-card-top"><strong>{row.name}</strong><span className={"disease-badge " + (row.pending ? "pending" : row.kind === "new" ? "new" : "done")}>{row.state}</span></div>
                  <small>{row.location} · {row.kind === "history" ? "历史记录 " : "本次记录 "}{row.year}</small>
                  <p>{row.summary}</p>
                  <span className="disease-card-link">{row.pending ? "记录本次变化" : row.kind === "new" ? "查看记录" : "查看复查结果"} <RightOutlined /></span>
                </button>)}
              </div> : <div className="disease-empty">
                <strong>上次未记录病害</strong>
                <p>请现场检查；发现病害可立即新增，未发现可直接在下方确认并完成。</p>
              </div>}
              {!isOwner ? <div className="readonly-note">此构件由 {inspectorName(ownerId)} 负责录入。{sharedSnapshot ? "本机已同步版本 " + sharedSnapshot.revision + "；队友新版本需再次同步。" : "本机尚未同步队友的本次记录。"}</div> : null}
            </div>
          )}

          {ready && screen === "review" && (
            <div className="screen-content review-page">
              {(() => {
                const history = component.history.find((item) => item.id === reviewId);
                if (!history) return <Empty description="未找到历史记录" />;
                return <>
                  <div className="page-intro"><h2>{history.indicatorName}</h2><p>{component.name} · {history.location}</p></div>
                  <div className="baseline-card"><span>上次正式记录 · {history.year} 年</span><p>{history.description}</p></div>
                  <div className="field-block"><label>病害变化 <b>*</b></label><div className="choice-grid">
                    {["无明显变化", "加重", "减轻", "本次未见", "无法判定"].map((value) => <button key={value} className={reviewDraft.change === value ? "selected" : ""} disabled={locked || !isOwner} onClick={() => setReviewDraft({ ...reviewDraft, change: value })}>{value}</button>)}
                  </div></div>
                  <div className="field-block"><label>维修情况 <b>*</b></label><div className="choice-grid three">
                    {["未见维修", "已维修", "不确定"].map((value) => <button key={value} className={reviewDraft.repair === value ? "selected" : ""} disabled={locked || !isOwner} onClick={() => setReviewDraft({ ...reviewDraft, repair: value })}>{value}</button>)}
                  </div></div>
                  <div className="field-block"><label htmlFor="review-observation">本次观察与说明</label><Input.TextArea id="review-observation" rows={4} placeholder="记录当前情况；本次未见或无法判定时请说明原因" value={reviewDraft.observation} disabled={locked || !isOwner} onChange={(event) => setReviewDraft({ ...reviewDraft, observation: event.target.value })} /></div>
                  <div className="notice-card">病害变化与维修情况分别记录；历史测量值不会自动成为本次测量值。</div>
                </>;
              })()}
            </div>
          )}


          {ready && screen === "chooser" && (
            <div className="chooser-page">
              <div className="chooser-top">
                <div className="chooser-context"><span>正在为构件选择病害</span><strong>{component.name}</strong><small>{component.secondary} · {bridgeName}</small></div>
                <Tabs className="chooser-tabs" activeKey={pickerTab} onChange={setPickerTab} items={[
                  { key: "all", label: "全部 " + available.length },
                  { key: "common", label: "常用" },
                  { key: "recent", label: "最近使用" },
                ]} />
                <Input className="chooser-search" size="large" prefix={<SearchOutlined />} value={search} onChange={(event) => setSearch(event.target.value)} placeholder="搜索病害名称或编号" aria-label="搜索病害名称或编号" allowClear />
                <div className="applicability">{inspectionYear} 年度评定树 · 仅展示当前构件适用项</div>
              </div>
              <div className="indicator-list">
                {indicatorSections.map((section) => <section key={section}>
                  <div className="chooser-section-head"><h3>{section}</h3><span>{filteredIndicators.filter((item) => displayGroup(item) === section).length} 项</span></div>
                  <div className="indicator-group">
                    {filteredIndicators.filter((item) => displayGroup(item) === section).map((item) => <button
                      key={item.id} className={"indicator-row " + (selectedId === item.id ? "selected" : "")}
                      onClick={() => setSelectedId(item.id)} aria-pressed={selectedId === item.id}>
                      <span className="indicator-label"><strong>{item.name}</strong><small>{item.number}</small></span>
                    </button>)}
                  </div>
                </section>)}
                {!filteredIndicators.length ? <Empty description={pickerTab === "recent" && !search ? "暂无最近使用的病害指标" : "未找到适用指标"} /> : null}
              </div>
            </div>
          )}

          {ready && screen === "form" && (
            <div className="screen-content form-page">
              <div className="compact-scope"><span className="scope-icon">桥</span><div><strong>{component.name} · {component.secondary}</strong><small>{bridgeName}</small></div></div>
              <div className="selected-indicator"><strong>{indicator?.name ?? "未选指标"}</strong><div><span>{indicator?.number}</span>{!locked && isOwner ? <button onClick={() => go("chooser")}>更换</button> : null}</div></div>
              <div className="field-block"><label htmlFor="defect-location">病害位置 <b>*</b></label><Input id="defect-location" size="large" placeholder="例如：第 1 跨梁底，距小桩号端 3 m" value={form.location} onChange={(event) => setForm({ ...form, location: event.target.value })} disabled={locked || !isOwner} /></div>
              {indicator?.name.includes("裂缝") || indicator?.name.includes("开裂") ? <div className="field-block"><label htmlFor="defect-direction">裂缝方向</label><Select id="defect-direction" size="large" placeholder="请选择" value={form.direction || undefined} onChange={(value) => setForm({ ...form, direction: value })} options={["横向", "纵向", "斜向", "网状", "其他"].map((value) => ({ label: value, value }))} disabled={locked || !isOwner} /></div> : null}
              <div className="section-head"><h3>本次测量</h3><span>可记录单值或范围</span></div>
              {form.measurements.map((item, index) => <div className="measure-card" key={index}>
                <div className="measure-heading"><strong>{item.label}</strong><label><input type="checkbox" checked={item.approximate} onChange={(event) => updateMeasurement(index, { approximate: event.target.checked })} disabled={locked || !isOwner} />约值</label></div>
                <div className="measure-inputs"><InputNumber min={0} controls={false} value={item.value ? Number(item.value) : null} onChange={(value) => updateMeasurement(index, { value: value == null ? "" : String(value) })} placeholder="数值" disabled={locked || !isOwner} /><span>至</span><InputNumber min={0} controls={false} value={item.end ? Number(item.end) : null} onChange={(value) => updateMeasurement(index, { end: value == null ? "" : String(value) })} placeholder="可选" disabled={locked || !isOwner} /><Select value={item.unit} onChange={(value) => updateMeasurement(index, { unit: value })} disabled={locked || !isOwner} options={["mm", "cm", "m", "m²", "%", "处"].map((value) => ({ label: value, value }))} /></div>
              </div>)}
              {!locked && isOwner ? <Button className="add-measure" type="dashed" icon={<PlusOutlined />} onClick={() => setForm({ ...form, measurements: [...form.measurements, { label: "补充测量", value: "", end: "", unit: "mm", approximate: false }] })}>添加另一测量值</Button> : null}
              <div className="section-head"><h3>现场照片</h3><span>{form.photos.length} / 5 张</span></div>
              <div className="photo-grid">
                {form.photos.map((photo) => <div className="photo-tile" key={photo.id}><img src={photo.dataUrl} alt={photo.name} />{!locked && isOwner ? <button aria-label={"移除照片" + photo.name} onClick={() => setForm({ ...form, photos: form.photos.filter((item) => item.id !== photo.id) })}><DeleteOutlined /></button> : null}</div>)}
                {!locked && isOwner && form.photos.length < 5 ? <button className="photo-add" onClick={() => photoInputRef.current?.click()}><CameraOutlined /><span>添加照片</span></button> : null}
              </div>
              <input ref={photoInputRef} className="visually-hidden" type="file" accept="image/*" multiple onChange={(event) => { void onPhotos(event); }} />
              {!form.photos.length ? <div className="field-block"><label htmlFor="photo-reason">暂缺照片原因</label><Input id="photo-reason" placeholder="无照片时请说明原因" value={form.photoReason} onChange={(event) => setForm({ ...form, photoReason: event.target.value })} disabled={locked || !isOwner} /></div> : null}
              <div className="field-block"><label htmlFor="defect-note">补充说明</label><Input.TextArea id="defect-note" rows={3} placeholder="可选，补充现场情况" value={form.note} onChange={(event) => setForm({ ...form, note: event.target.value })} disabled={locked || !isOwner} /></div>
              <div className="description-preview"><span>生成的病害描述预览</span><p>{descriptionFor({ ...form, id: editingId, createdAt: "" })}</p><small>描述由指标、位置和本次测量生成；补充说明单独保存。</small></div>
              {editingId && isOwner && !locked ? <Button danger block icon={<DeleteOutlined />} onClick={() => { const record = task.defects.find((item) => item.id === editingId); if (record) void deleteDefect(record).then((deleted) => { if (deleted) go("component"); }); }}>删除本次病害</Button> : null}
            </div>
          )}


          {ready && screen === "team" && (
            <div className="screen-content team-page">
              <div className="page-intro"><h2>{bridgeName} · 团队作业</h2><p>各自提交负责构件，手动同步队友记录</p></div>
              <div className="team-progress-card">
                <div><span>全桥构件已提交</span><strong>{submittedCount} / {components.length}</strong></div>
                <Progress percent={Math.round(submittedCount / components.length * 100)} showInfo={false} strokeColor="#9B3DF6" />
              </div>
              <div className="team-stage-card">
                <div className="stage-symbol"><CloudUploadOutlined /></div>
                <div><strong>我的构件 {myComponents.filter((item) => task.completed[item.id]).length} / {myComponents.length} 件已完成</strong><p>{myNeedsSubmit ? "完成自己负责的构件后，联网提交我的部分。" : "我的部分已提交；后续修改可重新提交新版本。"}</p></div>
              </div>
              <Button className="team-stage-action" type="primary" size="large" block disabled={!canSubmitMine} onClick={() => { void submitMine(); }}>
                {!online ? "联网后提交我的构件" : !myCompleted ? "完成我的构件后提交" : myNeedsSubmit ? "提交我的构件（模拟）" : "我的构件已提交"}
              </Button>
              <Button className="team-submit-action" icon={<SyncOutlined />} size="large" block disabled={!online || busy} onClick={() => { void syncTeam(); }}>
                同步队友记录{pendingRemoteCount ? " · " + pendingRemoteCount + " 件待同步" : ""}
              </Button>
              <div className="sync-meta">
                <span>已同步队友构件 {syncedCount} / {components.length - myComponents.length} 件</span>
                <span>{task.lastSyncedAt[activeInspectorId] ? "上次同步 " + new Date(task.lastSyncedAt[activeInspectorId]).toLocaleString("zh-CN", { hour12: false }) : "尚未同步"}</span>
              </div>
              <button className="text-link" onClick={() => go("upload")}>查看全部构件提交状态 <RightOutlined /></button>
              {pendingRemoteCount > 0 ? <Alert type="info" showIcon title={pendingRemoteCount + " 件队友记录可同步"} description="同步后可在病害清单和构件详情中只读查看；已保存在本机的旧版本仍可离线查看。" /> : null}
              <div className="section-head"><h3>现场作业人员</h3><span>{inspectors.length} 人分工</span></div>
              <div className="team-members">
                {inspectors.map((person) => {
                  const assigned = components.filter((item) => ownerByComponent[item.id] === person.id);
                  const submitted = assigned.filter((item) => Boolean(task.submissions[item.id])).length;
                  const synced = assigned.filter((item) => Boolean(task.syncedByInspector[activeInspectorId]?.[item.id])).length;
                  const done = person.id === activeInspectorId ? assigned.filter((item) => task.completed[item.id]).length : synced;
                  const stale = assigned.some((item) => task.submissions[item.id] && task.syncedByInspector[activeInspectorId]?.[item.id] && task.syncedByInspector[activeInspectorId][item.id].revision < task.submissions[item.id].revision);
                  const status = person.id === activeInspectorId ? submitted === assigned.length && !myNeedsSubmit ? "已提交" : "我的作业" : stale ? "有更新" : synced === assigned.length ? "已同步" : submitted ? "待同步" : "待提交";
                  return <details className="team-member" key={person.id}>
                    <summary>
                      <span className="member-avatar">{person.name.slice(0, 1)}</span>
                      <span className="member-main"><strong>{person.name} · {assigned.length} 件</strong><small>{person.id === activeInspectorId ? "已完成 " + done : "本机可见 " + done} · 已提交 {submitted}</small></span>
                      <span className={"member-state " + (status === "已提交" || status === "已同步" ? "staged" : "")}>{status}</span>
                      <RightOutlined />
                    </summary>
                    <div className="member-components">{assigned.map((item) => {
                      const mine = person.id === activeInspectorId;
                      const copy = task.syncedByInspector[activeInspectorId]?.[item.id];
                      const state = mine ? task.dirty[item.id] ? "修改待提交" : task.submissions[item.id] ? "已提交" : task.completed[item.id] ? "本机已完成" : "待检查" : copy ? task.submissions[item.id]?.revision > copy.revision ? "有更新" : "已同步" : task.submissions[item.id] ? "待同步" : "待队友提交";
                      return <button key={item.id} onClick={() => openComponent(item)}>{item.name}<span>{state}</span></button>;
                    })}</div>
                  </details>;
                })}
              </div>
              <div className={"network-banner " + (online ? "online" : "offline")}>
                {online ? "当前在线：提交和同步都由你点击后执行。" : "当前离线：继续采集，已同步的队友记录仍可查看；联网后再提交或同步。"}
              </div>
              <div className="demo-control-inline">
                <span>原型演示控制 · 切换身份模拟不同队员；提交与同步尚未连接真实服务器</span>
                <div>{inspectors.map((person) => <button key={person.id} className={activeInspectorId === person.id ? "active" : ""} onClick={() => setActiveInspectorId(person.id)}>{person.name}</button>)}</div>
                <Button size="small" onClick={() => setOnline(!online)}>{online ? "切换为离线" : "模拟恢复网络"}</Button>
              </div>
            </div>
          )}

          {ready && screen === "upload" && (
            <div className="screen-content upload-page">
              <div className="page-intro"><h2>提交与同步</h2><p>每位队员提交自己的构件，再同步队友已提交的记录</p></div>
              <div className="upload-summary">
                <div><strong>{myComponents.filter((item) => task.completed[item.id]).length} / {myComponents.length}</strong><span>我的构件已完成</span></div>
                <div><strong>{submittedCount} / {components.length}</strong><span>全桥已提交</span></div>
                <div><strong>{syncedCount} / {components.length - myComponents.length}</strong><span>队友已同步</span></div>
              </div>
              <Progress percent={Math.round(submittedCount / components.length * 100)} showInfo={false} strokeColor="#9B3DF6" />
              <div className="sync-action-stack">
                <Button type="primary" size="large" block disabled={!canSubmitMine} onClick={() => { void submitMine(); }}>
                  {!online ? "联网后提交我的构件" : !myCompleted ? "完成我的构件后提交" : myNeedsSubmit ? "提交我的构件（模拟）" : "我的构件已提交"}
                </Button>
                <Button icon={<SyncOutlined />} size="large" block disabled={!online || busy} onClick={() => { void syncTeam(); }}>
                  同步队友记录{pendingRemoteCount ? " · " + pendingRemoteCount + " 件待同步" : ""}
                </Button>
              </div>
              <div className="section-head"><h3>构件记录状态</h3><span>按分工独立提交</span></div>
              {components.map((item) => {
                const mine = ownerByComponent[item.id] === activeInspectorId;
                const copy = task.syncedByInspector[activeInspectorId]?.[item.id];
                const state = mine ? task.dirty[item.id] ? "修改待提交" : task.submissions[item.id] ? "已提交" : task.completed[item.id] ? "本机已完成" : "待检查" : copy ? "已同步" : task.submissions[item.id] ? "待同步" : "待队友提交";
                const ready = mine ? Boolean(task.submissions[item.id]) && !task.dirty[item.id] : Boolean(copy);
                return <button className="check-list-row" key={item.id} onClick={() => openComponent(item)}>
                  <span className={ready ? "check-ok" : "check-pending"}>{ready ? <CheckOutlined /> : "·"}</span>
                  <span>{item.name}<small>{inspectorName(ownerByComponent[item.id])}负责 · {item.secondary}</small></span>
                  <Tag color={ready ? "success" : "warning"}>{state}</Tag>
                </button>;
              })}
              <div className={"network-banner " + (online ? "online" : "offline")}>{online ? "手动同步会下载队友最近提交的版本；不会覆盖你的本地记录。" : "离线时可查看上次同步的副本，提交和同步按钮暂不可用。"}</div>
              <div className="notice-card">原型用本地数据模拟服务器提交和跨设备同步。正式系统应按构件版本提供提交回执，并只向有权限的队员同步只读记录。</div>
            </div>
          )}

          {ready && screen === "score" && (
            <div className="screen-content score-page">
              {!scoreReady ? <>
                <div className="score-wait"><CloudUploadOutlined /><h2>等待分工提交齐全</h2><p>还有 {components.length - submittedCount} 件构件未提交。各成员分别提交负责的构件后，由服务器汇总算分。</p></div>
                <Button block size="large" type="primary" onClick={() => go("team")}>查看团队提交进度</Button>
              </> : <>
                <div className="score-hero"><span>服务器试算 · 尚未正式确认</span><strong>88.6</strong><div className="score-grade"><span>技术状况等级</span><b>二类桥 · 演示</b></div><p>{bridgeName} · {inspectionYear} 年度 · {submittedCount} 件已提交</p></div>
                {hasLocalChanges ? <Alert type="warning" showIcon title="本机有尚未重新提交的修改" description="当前分数基于上次已提交的构件版本；请完成并重新提交修改后查看新结果。" /> : null}
                <div className="section-head"><h3>一级构件组</h3><span>服务器返回</span></div>
                {[["上部结构", 86.4], ["下部结构", 91.0], ["桥面系", 87.2]].map(([label, value]) => <div className="score-row" key={label}><div><strong>{label}</strong><b>{Number(value).toFixed(1)}</b></div><Progress percent={Number(value)} showInfo={false} strokeColor="#9B3DF6" /></div>)}
                <div className="section-head"><h3>二级构件组</h3><span>展开查看</span></div>
                {[["上部承重构件", 84.5], ["上部一般构件", 89.2], ["桥墩", 91.0], ["桥面铺装", 86.8], ["栏杆、护栏", 89.1]].map(([label, value]) => <div className="score-subrow" key={label}><span>{label}</span><strong>{Number(value).toFixed(1)}</strong></div>)}
                <div className="notice-card">本页数值仅用于演示交互。正式分数、等级和适用项必须以服务器算分结果为准。</div>
              </>}
            </div>
          )}

          {error ? <div className="screen-error" role="alert">{error}</div> : null}
        </div>

        {ready && ["component", "review", "chooser", "form"].includes(screen) ? <div className="sticky-footer">
          {screen === "component" ? <><Button type="primary" size="large" block disabled={locked || busy || !isOwner} onClick={() => {
            if (pendingHistory.length) openReview(pendingHistory[0].id);
            else void completeComponent();
          }}>{!isOwner ? "由" + inspectorName(ownerId) + "负责录入" : pendingHistory.length ? "继续复查 " + pendingHistory.length + " 条" : task.completed[component.id] ? "检查下一件 / 查看团队" : visibleDefects.length ? "完成本构件检查" : component.history.length ? "确认无新增病害并完成" : "确认无病害并完成"}</Button><small>本机保存检查结论；联网后由你主动提交</small></> : null}
          {screen === "review" ? <><Button type="primary" size="large" block disabled={locked || busy || !isOwner} onClick={() => { void saveReview(); }}>确认本次复查 · 保存本机</Button><small>历史记录保留，本次观测单独保存</small></> : null}
          {screen === "chooser" ? <Button type="primary" size="large" block disabled={!selectedId || !isOwner} onClick={nextToForm}>继续填写病害信息</Button> : null}
          {screen === "form" ? <><Button type="primary" size="large" block disabled={locked || busy || !isOwner} onClick={() => { void saveDefect(); }}>保存到本机</Button><small>联网后提交自己负责的构件</small></> : null}
        </div> : null}
        {ready && ["home", "components", "team", "upload", "score"].includes(screen) ? <nav className="bottom-nav" aria-label="主导航">
          {[["home", <HomeOutlined />, "概览"], ["components", <UnorderedListOutlined />, "构件"], ["team", <TeamOutlined />, "协作"], ["score", <CheckCircleFilled />, "评分"]].map(([target, icon, label]) => <button key={String(target)} className={screen === target || screen === "upload" && target === "team" ? "active" : ""} onClick={() => go(target as Screen)}>{icon}<span>{label}</span></button>)}
        </nav> : null}
        <Drawer
          title="原型演示设置"
          placement="bottom"
          size="58%"
          open={demoOpen}
          onClose={() => setDemoOpen(false)}
          getContainer={false}
          rootStyle={{ position: "absolute" }}
          className="demo-drawer"
        >
          <div className="demo-panel">
            <p>切换队员和网络状态，体验离线采集、个人提交与主动同步。以下交互仅为原型模拟。</p>
            <strong>演示身份</strong>
            <div className="demo-option-grid">{inspectors.map((person) => <button key={person.id} className={activeInspectorId === person.id ? "selected" : ""} onClick={() => setActiveInspectorId(person.id)}>{person.name}<small>{person.componentIds.length} 件构件</small></button>)}</div>
            <strong>网络状态</strong>
            <button className={"demo-network " + (online ? "online" : "")} onClick={() => setOnline(!online)}><span className="demo-network-dot" />{online ? "当前在线 · 点击切换离线" : "当前离线 · 点击恢复网络"}</button>
            <div className="demo-panel-actions">
              <Button type="primary" block onClick={() => { void fillDemo(); }}>填充多人协作示例</Button>
              <Button block onClick={() => { void resetDemo(); }}>重置本机演示数据</Button>
            </div>
          </div>
        </Drawer>
        <Drawer
          title="切换构件组"
          placement="bottom"
          size="62%"
          open={groupOpen}
          onClose={() => setGroupOpen(false)}
          getContainer={false}
          rootStyle={{ position: "absolute" }}
          className="group-drawer"
        >
          <div className="group-picker">
            <button className={"group-picker-all " + (!groupFilterActive ? "selected" : "")} onClick={() => { setGroupFilterActive(false); setComponentSearch(""); go("components"); }}>全桥构件 <span>{components.length} 件 · 先看病害</span></button>
            <div className="group-picker-heading"><strong>一级构件组</strong><span>按结构部位</span></div>
            <div className="group-picker-options primary">
              {primaryGroups.map((item) => <button key={item} className={groupFilterActive && primary === item ? "selected" : ""} onClick={() => {
                setGroupFilterActive(true);
                setPrimary(item);
                setSecondary(components.find((part) => part.primary === item)?.secondary ?? "");
                setComponentSearch("");
              }}>{item}<small>{components.filter((part) => part.primary === item).length} 件</small></button>)}
            </div>
            <div className="group-picker-heading"><strong>二级构件组</strong><span>{primary}</span></div>
            <div className="group-picker-options secondary">
              {secondaryGroups.map((item) => <button key={item} className={groupFilterActive && secondary === item ? "selected" : ""} onClick={() => {
                setGroupFilterActive(true);
                setSecondary(item);
                setComponentSearch("");
                setGroupOpen(false);
                go("components");
              }}>{item}<small>{currentGroup.filter((part) => part.secondary === item).length} 件构件</small></button>)}
            </div>
          </div>
        </Drawer>
      </div>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")!).render(
  <ConfigProvider locale={zhCN} theme={{ token: { colorPrimary: "#8D31F5", colorInfo: "#8D31F5", colorSuccess: "#218461", borderRadius: 10, fontSize: 14, controlHeight: 44, fontFamily: "Inter, PingFang SC, Microsoft YaHei, system-ui, sans-serif" } }}><App /></ConfigProvider>
);
























