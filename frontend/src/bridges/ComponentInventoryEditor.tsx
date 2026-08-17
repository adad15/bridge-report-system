import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  addComponentInventoryEntry,
  componentInventoryErrorMessage,
  confirmComponentInventory,
  confirmPendingComponentInventoryMappings,
  deactivateComponentInventoryEntry,
  deleteComponentInventoryEntry,
  fetchInventoryGroupEntries,
  fetchInventorySummary,
  generateComponentInventory,
  searchInventoryEntries,
  setComponentInventoryMapping,
  updateComponentInventoryEntry,
  type ComponentInventoryEntry,
  type GenerateComponentInventoryInput,
  type InventoryBlockerSummary,
  type InventoryEntryInput,
  type InventoryGroupEntriesResponse,
  type InventoryGroupSummary as ServerGroupSummary,
  type InventorySearchResponse,
  type InventorySummary,
  type InventoryWriteResult,
} from "../api/componentInventoryApi";
import {
  dropCached,
  inventoryCacheKey,
  readCached,
  standardCatalogsCacheKey,
  writeCached,
} from "../api/resourceCache";
import {
  fetchStandardMappingCatalogs,
  standardsErrorMessage,
  type StandardCatalog,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";
import { BridgeInventoryWizard } from "./BridgeInventoryWizard";
import { structurePartLabel, structurePartOrder } from "./structureParts";

interface MappingDraft {
  packageId: string;
  bridgeTypeId: string;
  categoryId: string;
}

function entryDraft(entry: ComponentInventoryEntry): InventoryEntryInput {
  return {
    component_number: entry.component_number,
    site_name: entry.site_name,
    site_component_type: entry.site_component_type,
    span_or_location: entry.span_or_location ?? "",
    remarks: entry.remarks ?? "",
  };
}

// "保存"原先只看 busy 和 is_active——没动过也能点，点一下就白发一次请求。
function draftIsDirty(entry: ComponentInventoryEntry, draft: InventoryEntryInput): boolean {
  const base = entryDraft(entry);
  return (Object.keys(base) as Array<keyof InventoryEntryInput>).some((key) => base[key] !== draft[key]);
}

// 确认前置校验与分组汇总都搬到服务端了：聚合之后前端拿不到构件级数据，
// "定位"按钮所需的 entity_id 与组内序号只能由服务端给出，规则也就只该有那一份实现。

function inventoryStatus(status: string) {
  return status === "已确认" ? "已确认" : status === "草稿" ? "草稿" : status;
}

export interface InventoryGroupSummary {
  siteComponentType: string;
  structurePart: string;
  activeCount: number;
  firstNumber: string;
  lastNumber: string;
  mappingLabel: string;
  confirmedCount: number;
  pendingCount: number;
  unmappedCount: number;
}

// 服务端返回的分组汇总转成界面用的形状。规范标签仍在前端解析：规范目录是另一条
// 请求，未到达前显示"—"而不是回退成 h21.component.* 这类原始 ID。
export function toGroupSummary(
  group: ServerGroupSummary,
  catalogs: StandardCatalog[]
): InventoryGroupSummary {
  let mappingLabel = "";
  if (catalogs.length > 0 && group.standard_component_category_id) {
    const catalog =
      catalogs.find((item) => item.package.id === group.standard_package_id) ??
      catalogs.find((item) => item.component_categories.some(
        (category) => category.id === group.standard_component_category_id
      ));
    const category = catalog?.component_categories.find(
      (item) => item.id === group.standard_component_category_id
    );
    mappingLabel = `${catalog?.package.standard_code ?? "技术评定规范"} · ${
      category?.name ?? group.standard_component_category_id
    }`;
  }
  return {
    siteComponentType: group.site_component_type,
    structurePart: group.structure_part,
    activeCount: group.active_count,
    // 整组停用时服务端给 null；界面按空串走原有的破折号分支。
    firstNumber: group.first_number ?? "",
    lastNumber: group.last_number ?? "",
    mappingLabel,
    confirmedCount: group.confirmed_count,
    pendingCount: group.pending_count,
    unmappedCount: group.unmapped_count,
  };
}

// 全部确认时只给一个对勾：数量列已经写了同一个数字，再写"已确认 N"没有信息量。
// 只有存在待确认或无映射时才列出问题项。
// 原先这个函数一切正常时返回 "✓"，撑起一整列"映射状态"——而一座桥的构件分组通常
// 全都映射好了，那列就是一竖排 ✓，占着宽度不带任何信息。现在只回报异常，正常返回
// null，由调用方决定不渲染。
export function groupAnomalyText(group: InventoryGroupSummary): string | null {
  const parts: string[] = [];
  if (group.pendingCount > 0) parts.push(`待确认 ${group.pendingCount}`);
  if (group.unmappedCount > 0) parts.push(`无映射 ${group.unmappedCount}`);
  return parts.length > 0 ? parts.join("、") : null;
}

const kSearchDebounceMs = 250;
const kEntriesPageSize = 100;
const kMaxSearchResults = 50;

export function ComponentInventoryEditor({ bridgeId }: { bridgeId: string }) {
  // 首屏只要分组汇总；构件明细与搜索结果按需取，不再把整份台账放进内存。
  const [summary, setSummary] = useState<InventorySummary | null>(null);
  const [groupEntries, setGroupEntries] = useState<InventoryGroupEntriesResponse | null>(null);
  const [groupEntriesLoading, setGroupEntriesLoading] = useState(false);
  const [searchResults, setSearchResults] = useState<InventorySearchResponse | null>(null);
  const [searchLoading, setSearchLoading] = useState(false);
  // 写操作成功后自增，触发当前分组页与搜索结果重取。
  const [refreshToken, setRefreshToken] = useState(0);
  const revision = summary?.revision ?? null;
  const [notCreated, setNotCreated] = useState(false);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [plan, setPlan] = useState<GenerateComponentInventoryInput | null>(null);
  const [drafts, setDrafts] = useState<Record<string, InventoryEntryInput>>({});
  const [deactivationReasons, setDeactivationReasons] = useState<Record<string, string>>({});
  const [mappingDrafts, setMappingDrafts] = useState<Record<string, MappingDraft>>({});
  const [catalogs, setCatalogs] = useState<StandardCatalog[]>(
    () => readCached<StandardCatalog[]>(standardCatalogsCacheKey) ?? []
  );
  const [catalogLoading, setCatalogLoading] = useState(
    () => readCached<StandardCatalog[]>(standardCatalogsCacheKey) === undefined
  );
  // 规范目录取不到时，规范映射列会全是"—"；单独记错误并提示，避免无从判断。
  const [catalogError, setCatalogError] = useState<string | null>(null);
  const [adding, setAdding] = useState(false);
  const [newEntry, setNewEntry] = useState<InventoryEntryInput>({
    component_number: "", site_name: "", site_component_type: "", span_or_location: "", remarks: "",
  });
  const [expandedGroup, setExpandedGroup] = useState<string | null>(null);
  const [groupPage, setGroupPage] = useState(0);
  const [search, setSearch] = useState("");
  // 一次只编辑一行。构件动辄上千个，绝大多数只是被翻阅，不该整页都摆成输入框。
  const [editingEntryId, setEditingEntryId] = useState<string | null>(null);
  // 停用原因只在真要停用时才问，不再每行常驻一个空输入框。
  const [deactivatingId, setDeactivatingId] = useState<string | null>(null);
  const [pendingFocusId, setPendingFocusId] = useState<string | null>(null);
  const rowRefs = useRef<Record<string, HTMLTableRowElement | null>>({});

  const load = useCallback(async () => {
    // 有上次的结果就先渲染它、不显示"加载中"，再在后台重新校验：
    // 台账有数千条构件，每次切回页签都从头等一遍不符合正常网页的观感。
    const cacheKey = inventoryCacheKey(bridgeId);
    const cached = readCached<InventorySummary>(cacheKey);
    if (cached) {
      setSummary(cached);
      setNotCreated(false);
      setLoading(false);
    } else {
      setLoading(true);
    }
    setError(null);
    try {
      const latest = await fetchInventorySummary(backendBaseUrl, bridgeId);
      writeCached(cacheKey, latest);
      setSummary(latest);
      setNotCreated(false);
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "component_inventory_not_found") {
        dropCached(cacheKey);
        setSummary(null);
        setNotCreated(true);
      } else {
        setError(componentInventoryErrorMessage(caught));
      }
    } finally {
      setLoading(false);
    }
  }, [bridgeId]);

  useEffect(() => { void load(); }, [load]);

  // 草稿只覆盖"当前屏幕上有的"构件。原来是给全部五千多条各建一份，翻不到的那些
  // 永远用不上。
  useEffect(() => {
    const loaded = [...(groupEntries?.entries ?? []), ...(searchResults?.entries ?? [])];
    if (loaded.length === 0) return;
    setDrafts((current) => {
      const next = { ...current };
      for (const entry of loaded) next[entry.id] = entryDraft(entry);
      return next;
    });
  }, [groupEntries, searchResults]);


  // 打开分组、翻页、或写操作之后：取该组的一页构件。
  // AbortController 是必须的——防抖只减少请求数，保证不了返回顺序：快速翻页时
  // 先发的请求后返回，就会把新页覆盖回旧页。
  useEffect(() => {
    if (!revision || !expandedGroup) { setGroupEntries(null); return; }
    const controller = new AbortController();
    setGroupEntriesLoading(true);
    fetchInventoryGroupEntries(
      backendBaseUrl, revision.id, expandedGroup, groupPage, kEntriesPageSize, controller.signal)
      .then((next) => {
        if (controller.signal.aborted) return;
        setGroupEntries(next);
        // 删构件后该组可能变短甚至清空：按新 total 夹取页码，归零则关掉弹窗。
        if (next.total === 0) { setExpandedGroup(null); return; }
        const lastPage = Math.max(0, Math.ceil(next.total / kEntriesPageSize) - 1);
        if (groupPage > lastPage) setGroupPage(lastPage);
      })
      .catch((caught) => {
        if (controller.signal.aborted) return;
        setError(componentInventoryErrorMessage(caught));
      })
      .finally(() => { if (!controller.signal.aborted) setGroupEntriesLoading(false); });
    return () => controller.abort();
  }, [revision?.id, expandedGroup, groupPage, refreshToken]);

  // 编号搜索：防抖 250ms，同样用 AbortController 防乱序。
  useEffect(() => {
    const term = search.trim();
    if (!revision || !term) { setSearchResults(null); return; }
    const controller = new AbortController();
    const timer = window.setTimeout(() => {
      setSearchLoading(true);
      searchInventoryEntries(
        backendBaseUrl, revision.id, term, kMaxSearchResults, controller.signal)
        .then((next) => { if (!controller.signal.aborted) setSearchResults(next); })
        .catch((caught) => {
          if (!controller.signal.aborted) setError(componentInventoryErrorMessage(caught));
        })
        .finally(() => { if (!controller.signal.aborted) setSearchLoading(false); });
    }, kSearchDebounceMs);
    return () => { controller.abort(); window.clearTimeout(timer); };
  }, [revision?.id, search, refreshToken]);

  // 映射页只需要规范身份、桥型和构件类别。使用一个轻量聚合接口，避免先取规范包列表，
  // 再为每个版本下载包含全部病害指标的完整目录。
  useEffect(() => {
    let cancelled = false;
    const cached = readCached<StandardCatalog[]>(standardCatalogsCacheKey);
    if (cached) {
      setCatalogs(cached);
      setCatalogLoading(false);
    } else {
      setCatalogLoading(true);
    }
    fetchStandardMappingCatalogs(backendBaseUrl)
      .then((loaded) => {
        writeCached(standardCatalogsCacheKey, loaded);
        if (!cancelled) {
          setCatalogs(loaded);
          setCatalogError(loaded.length === 0 ? "未取到可用的技术评定规范包。" : null);
        }
      })
      .catch((caught) => {
        // 台账读取与编辑不因目录展示失败而整体失效，但也不能静默：
        // 之前这里吞掉异常，规范映射列会永久停在"—"，从界面上看不出任何原因。
        if (!cancelled) setCatalogError(standardsErrorMessage(caught));
      })
      .finally(() => {
        if (!cancelled) setCatalogLoading(false);
      });
    return () => { cancelled = true; };
  }, []);

  const groupSummaries = useMemo(
    () => (summary?.groups ?? []).map((group) => toGroupSummary(group, catalogs)),
    [summary, catalogs]
  );
  // 按结构分部分段，顺序与向导一致（上部 → 下部 → 桥面系），空分部不显示。
  const groupSections = useMemo(
    () =>
      structurePartOrder
        .map((key) => ({
          key,
          groups: groupSummaries.filter((group) => group.structurePart === key),
        }))
        .filter((section) => section.groups.length > 0),
    [groupSummaries]
  );
  const blockerTotal = summary?.blockers.total ?? 0;
  const pendingMappingCount = useMemo(
    () => groupSummaries.reduce((total, group) => total + group.pendingCount, 0),
    [groupSummaries]
  );
  // 服务端已经按两级拆好：待确认那批只进计数、由上面那行代表，样本里只有无映射构件
  // 和空台账。这里不用再过滤一遍。
  const individualBlockers = summary?.blockers.samples ?? [];
  // "其余 N 项"必须用 individual_total 减样本数。用 total 会把待确认那批数两遍——
  // 100 个待确认、0 个无映射时，页面会同时显示"100 个待确认"和"其余 100 项"。
  const remainingBlockers =
    (summary?.blockers.individual_total ?? 0) - individualBlockers.length;
  const searchTerm = search.trim();
  const searchMatches = searchResults?.entries ?? [];
  const expandedGroupMapping = useMemo(
    () => groupSummaries.find((group) => group.siteComponentType === expandedGroup)?.mappingLabel ?? "",
    [groupSummaries, expandedGroup]
  );
  const groupTotal = groupEntries?.total ?? 0;
  const pageCount = Math.max(1, Math.ceil(groupTotal / kEntriesPageSize));
  const page = Math.min(groupPage, pageCount - 1);
  const pageEntries = groupEntries?.entries ?? [];

  useEffect(() => {
    if (!pendingFocusId) return;
    const row = rowRefs.current[pendingFocusId];
    if (row) {
      row.scrollIntoView({ behavior: "smooth", block: "center" });
      row.focus();
      setPendingFocusId(null);
    }
  }, [pendingFocusId, expandedGroup, page, revision]);

  async function mutate(action: () => Promise<InventoryWriteResult>) {
    setBusy(true);
    setError(null);
    try {
      const next = await action();
      // 顺序不能颠倒：先换汇总（里面带着可能已经变了的修订版 id），再按新 id 重取
      // 明细与搜索，否则会拿旧 id 去请求。
      const nextSummary: InventorySummary = {
        revision: next.revision, groups: next.groups, blockers: next.blockers,
      };
      writeCached(inventoryCacheKey(bridgeId), nextSummary);
      setSummary(nextSummary);
      setNotCreated(false);
      // 只把响应里那条构件补进当前页是不够的：删除和批量确认根本不带构件，
      // 改类别还会让构件换组、改变分页集合。统一重取当前打开的那一组与搜索结果。
      setRefreshToken((token) => token + 1);
      return true;
    } catch (caught) {
      // 手里这个修订版已经不可写：桥上已有基于其他版本的草稿。只弹一句提示的话，
      // 用户会对着同一个不可写的 id 反复点同一个按钮，所以直接重新拉取，
      // 让后续操作落在最新草稿上。load() 会先清空 error，故提示放在它之后。
      if (caught instanceof ApiError && caught.code === "inventory_revision_superseded") {
        await load();
        setError(componentInventoryErrorMessage(caught));
        return false;
      }
      setError(componentInventoryErrorMessage(caught));
      if (caught instanceof ApiError) {
        // 不能盲取 samples[0]：inventory_empty 排在最前，而它是修订版级问题，
        // 没有组内序号；blocker 全是待确认映射时样本还可能为空。
        const details = caught.details as { blockers?: InventoryBlockerSummary } | undefined;
        const locatable = details?.blockers?.samples?.find(
          (sample) => sample.entity_type === "inventory_entry" && sample.position !== null);
        if (locatable) focusEntry(locatable);
      }
      return false;
    } finally {
      setBusy(false);
    }
  }

  // 定位到某个构件：服务端给的是组内序号，页码在这里算。找不到可定位的样本时
  // 只展示错误，不猜页码。
  function focusEntry(sample: { site_component_type: string | null; position: number | null;
                                entity_id: string }) {
    if (sample.site_component_type === null || sample.position === null) return;
    setSearch("");
    setExpandedGroup(sample.site_component_type);
    setGroupPage(Math.floor(sample.position / kEntriesPageSize));
    setPendingFocusId(sample.entity_id);
    // 从"确认前还需处理"跳过来就是奔着改这一行去的，直接开编辑态，省一次点击。
    setEditingEntryId(sample.entity_id);
    setDeactivatingId(null);
  }

  function openGroup(siteComponentType: string) {
    setGroupPage(0);
    setExpandedGroup(siteComponentType);
  }

  async function generate() {
    if (!plan) return;
    await mutate(() => generateComponentInventory(backendBaseUrl, bridgeId, plan));
  }

  async function saveEntry(entry: ComponentInventoryEntry) {
    const draft = drafts[entry.id];
    if (!revision || !draft) return;
    const ok = await mutate(() => updateComponentInventoryEntry(backendBaseUrl, revision.id, entry.id, draft));
    if (ok) closeEditor();
  }

  function beginEdit(entry: ComponentInventoryEntry) {
    // 切换行时把上一行的未保存改动丢回原值，免得留下看不见的脏草稿。
    cancelEdit();
    setEditingEntryId(entry.id);
  }

  function cancelEdit() {
    // 只在当前加载出来的构件里找——全量表已经不在内存里了。
    const current = editingEntryId
      ? [...pageEntries, ...searchMatches].find((entry) => entry.id === editingEntryId)
      : undefined;
    if (current) setDrafts((drafts) => ({ ...drafts, [current.id]: entryDraft(current) }));
    closeEditor();
  }

  function closeEditor() {
    setEditingEntryId(null);
    setDeactivatingId(null);
  }

  async function addEntry() {
    if (!revision || !newEntry.component_number.trim() || !newEntry.site_name.trim() || !newEntry.site_component_type.trim()) return;
    const ok = await mutate(() => addComponentInventoryEntry(backendBaseUrl, revision.id, newEntry));
    if (ok) {
      setNewEntry({ component_number: "", site_name: "", site_component_type: "", span_or_location: "", remarks: "" });
      setAdding(false);
    }
  }

  async function confirmPendingMappings(siteComponentType?: string) {
    if (!revision) return;
    await mutate(() =>
      confirmPendingComponentInventoryMappings(backendBaseUrl, revision.id, siteComponentType));
  }

  async function confirmExistingMapping(entry: ComponentInventoryEntry) {
    if (!revision) return;
    // 确认的是一条明确选定的待确认映射，不是"首个生效映射"原样重交——多映射时
    // 首个可能已经是已确认的，重交它等于什么也没做。
    const mapping = entry.mappings.find(
      (item) => item.is_active && item.confirmation_status !== "已确认");
    if (!mapping) return;
    await mutate(() => setComponentInventoryMapping(backendBaseUrl, revision.id, entry.id, {
      standard_package_id: mapping.standard_package_id,
      standard_bridge_type_id: mapping.standard_bridge_type_id,
      standard_component_category_id: mapping.standard_component_category_id,
      structure_part: mapping.structure_part,
      mapping_source: "用户确认",
    }));
  }

  function beginMapping(entryId: string) {
    const first = catalogs[0];
    const bridgeType = first?.bridge_types[0];
    setMappingDrafts((current) => ({
      ...current,
      [entryId]: { packageId: first?.package.id ?? "", bridgeTypeId: bridgeType?.id ?? "", categoryId: "" },
    }));
  }

  function updateMappingDraft(entryId: string, update: Partial<MappingDraft>) {
    setMappingDrafts((current) => ({ ...current, [entryId]: { ...current[entryId], ...update } }));
  }

  async function saveMapping(entryId: string) {
    if (!revision) return;
    const draft = mappingDrafts[entryId];
    const catalog = catalogs.find((item) => item.package.id === draft?.packageId);
    const category = catalog?.component_categories.find((item) => item.id === draft?.categoryId);
    if (!draft || !category) return;
    const ok = await mutate(() => setComponentInventoryMapping(backendBaseUrl, revision.id, entryId, {
      standard_package_id: draft.packageId,
      standard_bridge_type_id: draft.bridgeTypeId,
      standard_component_category_id: draft.categoryId,
      structure_part: category.structure_part,
      mapping_source: "人工选择",
    }));
    if (ok) setMappingDrafts((current) => { const next = { ...current }; delete next[entryId]; return next; });
  }

  /* 只读是常态、编辑是例外：一组构件动辄上百上千，原先每行常驻三个输入框加一整列
     按钮，行高被撑到 123px，翻一页要滚一万多像素。现在平时只渲染文本，点"编辑"才把
     那一行展开成编辑区。showType 给搜索结果用——那里跨类别混排，类别列有信息量；
     分组弹窗里整组同一个类别，825 行印 825 遍没有意义。 */
  function renderEntryRow(entry: ComponentInventoryEntry, options?: { showType?: boolean }) {
    if (!revision) return null;
    const showType = options?.showType ?? false;
    const draft = drafts[entry.id] ?? entryDraft(entry);
    /* 一个构件可以按规范包挂多个生效映射（唯一索引是 entry + package）。状态要按
       "存在任一已确认"判断，和汇总、confirm 是同一套口径；只看首个映射的话，
       [待确认(包A)、已确认(包B)] 会在一个"全部已确认"的分组里显示成待确认，
       还允许再确认一次 A。 */
    const activeMappings = entry.mappings.filter((item) => item.is_active);
    const activeMapping = activeMappings[0];
    const hasConfirmedMapping = activeMappings.some((item) => item.confirmation_status === "已确认");
    const mappingDraft = mappingDrafts[entry.id];
    const mappingCatalog = catalogs.find((item) => item.package.id === mappingDraft?.packageId);
    const mappingCategories = mappingCatalog?.component_categories.filter(
      (item) => item.bridge_type_ids.includes(mappingDraft?.bridgeTypeId ?? "")
    ) ?? [];
    const rowProps = {
      ref: (node: HTMLTableRowElement | null) => { rowRefs.current[entry.id] = node; },
      tabIndex: -1,
    };

    if (editingEntryId !== entry.id) {
      const reason = !entry.is_active ? "已停用"
        : !activeMapping ? "无映射"
        : !hasConfirmedMapping ? "待确认映射"
        : null;
      return (
        <tr key={entry.id} {...rowProps} className={!entry.is_active ? "inventory-entry-inactive" : undefined}>
          <td>{entry.component_number}</td>
          {showType ? <td>{entry.site_component_type}</td> : null}
          <td>{entry.span_or_location || "—"}</td>
          <td className="inventory-entry-rowend">
            {reason ? (
              <span className={`status-badge ${entry.is_active ? "status-badge-warn" : "status-badge-neutral"}`}>{reason}</span>
            ) : null}
            <button type="button" disabled={busy} onClick={() => beginEdit(entry)}>
              编辑
            </button>
          </td>
        </tr>
      );
    }

    const dirty = draftIsDirty(entry, draft);
    const deactivating = deactivatingId === entry.id;
    const reasonText = deactivationReasons[entry.id] ?? "";
    return (
      <tr key={entry.id} {...rowProps} className="inventory-entry-editing">
        <td colSpan={showType ? 4 : 3}>
          <div className="inventory-entry-editor">
            <label>
              构件编号
              <input aria-label={`构件编号 ${entry.component_number}`} value={draft.component_number} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, component_number: event.target.value } }))} />
            </label>
            {/* 现场名称与构件类别在生成时就是同一个值，页面不再单列，改类别时同步跟随。 */}
            <label>
              构件类别
              <input aria-label={`构件类别 ${entry.component_number}`} value={draft.site_component_type} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, site_component_type: event.target.value, site_name: event.target.value } }))} />
            </label>
            <label>
              所属跨或位置
              <input aria-label={`所属跨或位置 ${entry.component_number}`} value={draft.span_or_location ?? ""} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, span_or_location: event.target.value } }))} />
            </label>
            <div className="inventory-row-actions inventory-entry-editor-actions">
              <button type="button" className="primary-button" disabled={busy || !entry.is_active || !dirty} onClick={() => void saveEntry(entry)}>保存</button>
              <button type="button" disabled={busy} onClick={cancelEdit}>取消</button>
              {/* 低频且不可逆的动作收进菜单，不和保存抢视觉权重。 */}
              <details className="more-actions">
                <summary aria-label={`更多操作 ${entry.component_number}`}>更多</summary>
                <div>
                  {/* 已确认的映射整组一致、分组核对表已经显示，这里只在需要处理时才出现。 */}
                  {activeMapping && !hasConfirmedMapping ? (
                    <button type="button" disabled={busy} onClick={() => void confirmExistingMapping(entry)}>确认映射</button>
                  ) : null}
                  {!activeMapping && !mappingDraft ? (
                    <button type="button" disabled={busy || catalogs.length === 0} onClick={() => beginMapping(entry.id)}>设置规范映射</button>
                  ) : null}
                  {entry.is_active && entry.is_referenced ? (
                    <button type="button" disabled={busy} onClick={() => setDeactivatingId(entry.id)}>停用</button>
                  ) : null}
                  {entry.is_active && !entry.is_referenced ? (
                    <button type="button" className="danger-menu-item" disabled={busy} onClick={() => void mutate(() => deleteComponentInventoryEntry(backendBaseUrl, revision.id, entry.id))}>删除</button>
                  ) : null}
                </div>
              </details>
            </div>
          </div>

          {!activeMapping && mappingDraft ? (
            <div className="inventory-mapping-editor">
              <select aria-label={`映射规范 ${entry.component_number}`} value={mappingDraft.packageId} onChange={(event) => {
                const nextCatalog = catalogs.find((item) => item.package.id === event.target.value);
                updateMappingDraft(entry.id, { packageId: event.target.value, bridgeTypeId: nextCatalog?.bridge_types[0]?.id ?? "", categoryId: "" });
              }}>
                {catalogs.map((item) => <option key={item.package.id} value={item.package.id}>{item.package.standard_code}</option>)}
              </select>
              <select aria-label={`映射桥型 ${entry.component_number}`} value={mappingDraft.bridgeTypeId} onChange={(event) => updateMappingDraft(entry.id, { bridgeTypeId: event.target.value, categoryId: "" })}>
                {mappingCatalog?.bridge_types.map((item) => <option key={item.id} value={item.id}>{item.name}</option>)}
              </select>
              <select aria-label={`映射类别 ${entry.component_number}`} value={mappingDraft.categoryId} onChange={(event) => updateMappingDraft(entry.id, { categoryId: event.target.value })}>
                <option value="">请选择类别</option>
                {mappingCategories.map((item) => <option key={item.id} value={item.id}>{item.name}</option>)}
              </select>
              <button type="button" disabled={busy || !mappingDraft.categoryId} onClick={() => void saveMapping(entry.id)}>保存映射</button>
            </div>
          ) : null}

          {deactivating ? (
            <div className="inventory-deactivate-row">
              <input aria-label={`停用原因 ${entry.component_number}`} placeholder="停用原因" value={reasonText} onChange={(event) => setDeactivationReasons((current) => ({ ...current, [entry.id]: event.target.value }))} />
              <button type="button" className="danger-button" disabled={busy || !reasonText.trim()} onClick={() => void mutate(() => deactivateComponentInventoryEntry(backendBaseUrl, revision.id, entry.id, reasonText.trim()))}>确认停用</button>
              <button type="button" disabled={busy} onClick={() => setDeactivatingId(null)}>取消停用</button>
            </div>
          ) : null}
        </td>
      </tr>
    );
  }

  if (loading || (revision !== null && catalogLoading))
    return <section className="workspace-card"><h1>实际构件台账</h1><p>正在加载台账与规范映射…</p></section>;

  if (notCreated) {
    return (
      <section className="workspace-card component-inventory-panel">
        <h1>实际构件台账</h1>
        <p>这座桥还没有构件台账。填写数量后，系统会生成每一个实际构件编号。</p>
        <BridgeInventoryWizard onPlanChange={setPlan} />
        {error ? <p className="error-text" role="alert">{error}</p> : null}
        <div className="inventory-panel-actions">
          <button type="button" disabled={busy || !plan} onClick={() => void generate()}>
            {busy ? "正在生成…" : "生成初始构件台账"}
          </button>
        </div>
      </section>
    );
  }

  if (!revision) {
    return <section className="workspace-card"><h1>实际构件台账</h1><p className="error-text">{error ?? "加载构件台账失败。"}</p><button type="button" onClick={() => void load()}>重试</button></section>;
  }

  return (
    <section className="workspace-card component-inventory-panel">
      <div className="inventory-section-heading">
        <div>
          <p className="section-kicker">版本 {revision.revision_number}</p>
          <h1>实际构件台账</h1>
        </div>
        <span className={`inventory-status-badge ${revision.status === "已确认" ? "confirmed" : ""}`}>
          {inventoryStatus(revision.status)}
        </span>
      </div>
      {/* 这两句是读一次就够的规则说明，不是状态，却常驻在表格上方吃掉约 100px 首屏。
          折进来，需要时再展开。 */}
      <details className="inventory-rules">
        <summary>关于编号与版本的说明</summary>
        <div>
          <p>构件编号和现场名称可修改。内部实际构件 ID 不在页面显示，修改编号不会改变其身份。</p>
          {revision.status === "已确认" ? <p>修改已确认台账时，系统会自动创建下一版草稿，原确认版本保持不变。</p> : null}
        </div>
      </details>

      {/* 搜索是进入数据的入口，原先却排在分组核对表和确认摘要之后——要找一个构件得先
          翻过整张表。入口挪到表格前面，结果也就紧挨着输入框显示。底部只留"手动添加
          构件"和"确认本版台账"：确认是看完表格才做的终点动作，不能排在被确认的内容前面。 */}
      <div className="inventory-entry-tools">
        <label>
          按编号搜索构件
          <input
            aria-label="按编号搜索构件"
            placeholder="如 3-5#"
            value={search}
            onChange={(event) => setSearch(event.target.value)}
          />
        </label>
        {/* 提示语和搜索框是一件事，拆成两条横带只会让卡片显得散。搜索时让位给结果计数。 */}
        {searchTerm ? null : (
          <p className="inventory-entry-hint">输入构件编号可直接定位单个构件，或在分组核对表中点击“查看构件”，在弹窗中查看并编辑该组构件。</p>
        )}
      </div>
      {searchTerm ? (
        <div className="inventory-entry-section">
          <h2>搜索结果</h2>
          <p>
            匹配 {searchMatches.length} 个构件
            {searchMatches.length > kMaxSearchResults ? `，显示前 ${kMaxSearchResults} 个` : ""}。
          </p>
          <div className="inventory-table-scroll">
            <table className="data-table component-inventory-table">
              {/* 搜索跨类别混排，类别列在这里有信息量，保留。 */}
              <thead><tr><th>构件编号</th><th>构件类别</th><th>所属跨或位置</th><th></th></tr></thead>
              <tbody>{searchMatches.slice(0, kMaxSearchResults).map((entry) => renderEntryRow(entry, { showType: true }))}</tbody>
            </table>
          </div>
        </div>
      ) : null}

      {groupSummaries.length > 0 ? (
        <div className="inventory-group-summary">
          <h2>分组核对</h2>
          {catalogError ? (
            <p className="inventory-standard-notice" role="status">
              规范映射名称暂时取不到（{catalogError}）；构件与编号不受影响，可稍后重试。
            </p>
          ) : null}
          <div className="inventory-table-scroll">
            <table className="data-table">
              <thead>
                <tr><th>构件类别</th><th className="numeric-cell">数量</th><th>编号范围</th><th>规范映射</th><th>操作</th></tr>
              </thead>
              {groupSections.map((section) => (
                <tbody key={section.key}>
                  <tr className="inventory-structure-row">
                    <th scope="colgroup" colSpan={5}>{structurePartLabel(section.key)}</th>
                  </tr>
                  {section.groups.map((group) => {
                  const anomaly = groupAnomalyText(group);
                  return (
                  <tr key={group.siteComponentType}>
                    <td>{group.siteComponentType}</td>
                    <td className="numeric-cell">{group.activeCount}</td>
                    <td>
                      {group.firstNumber}
                      {group.activeCount > 1 ? ` … ${group.lastNumber}` : ""}
                    </td>
                    {/* 异常挂在映射本身旁边，比单开一列更好读——那列在正常情况下是一竖排 ✓。 */}
                    <td>
                      {group.mappingLabel || "—"}
                      {anomaly ? <span className="inventory-mapping-flag">{anomaly}</span> : null}
                    </td>
                    <td>
                      <div className="inventory-row-actions">
                        <button
                          type="button"
                          aria-label={`查看构件 ${group.siteComponentType}`}
                          onClick={() => openGroup(group.siteComponentType)}
                        >
                          查看构件
                        </button>
                        {group.pendingCount > 0 && revision.status === "草稿" ? (
                          <button
                            type="button"
                            disabled={busy}
                            onClick={() => void confirmPendingMappings(group.siteComponentType)}
                          >
                            确认该组映射
                          </button>
                        ) : null}
                      </div>
                    </td>
                  </tr>
                  );
                  })}
                </tbody>
              ))}
            </table>
          </div>
        </div>
      ) : null}
      {blockerTotal > 0 ? (
        <div className="inventory-blockers" role="status">
          <strong>确认前还需处理 {blockerTotal} 项</strong>
          <ul>
            {pendingMappingCount > 0 ? (
              <li>
                {pendingMappingCount} 个构件的规范映射待确认；可在分组核对表按组确认，或
                <button
                  type="button"
                  disabled={busy || revision.status !== "草稿"}
                  onClick={() => void confirmPendingMappings()}
                >
                  一键确认全部待确认映射
                </button>
              </li>
            ) : null}
            {/* 样本已由服务端截断到 30 条，这里不再自己 slice。 */}
            {individualBlockers.map((blocker, index) => (
              <li key={`${blocker.code}-${blocker.entity_id}-${index}`}>
                {blocker.message}
                {blocker.entity_type === "inventory_entry" && blocker.position !== null ? (
                  <button type="button" onClick={() => focusEntry(blocker)}>定位</button>
                ) : null}
              </li>
            ))}
            {remainingBlockers > 0 ? (
              <li>……其余 {remainingBlockers} 项处理后依次显示。</li>
            ) : null}
          </ul>
        </div>
      ) : (
        <p className="inventory-confirmation-summary">共 {revision.active_entry_count} 个启用构件，规范映射均已确认。</p>
      )}
      {error && !expandedGroup ? <p className="error-text" role="alert">{error}</p> : null}
      {expandedGroup ? (
        <div className="dialog-backdrop" role="presentation">
          <section
            className="workspace-dialog inventory-group-dialog"
            role="dialog"
            aria-modal="true"
            aria-labelledby="inventory-group-dialog-title"
          >
            <div className="inventory-group-dialog-header">
              <h2 id="inventory-group-dialog-title">
                {expandedGroup} 构件（共 {groupTotal} 个）
              </h2>
              {/* 整组共用同一个规范映射，在标题处说明一次，不再逐行重复。 */}
              {expandedGroupMapping ? (
                <p className="inventory-group-dialog-mapping">规范映射：{expandedGroupMapping}</p>
              ) : null}
              {error ? <p className="error-text" role="alert">{error}</p> : null}
            </div>
            <div className="inventory-table-scroll inventory-group-dialog-body">
              <table className="data-table component-inventory-table">
                {/* 整组共用同一个类别（标题已写明），不再逐行重复；改类别在编辑态里做。 */}
                <thead><tr><th>构件编号</th><th>所属跨或位置</th><th></th></tr></thead>
                <tbody>{pageEntries.map((entry) => renderEntryRow(entry))}</tbody>
              </table>
            </div>
            <div className="inventory-group-dialog-footer">
              {pageCount > 1 ? (
                <div className="inventory-pagination">
                  <button type="button" disabled={busy || page === 0} onClick={() => setGroupPage(page - 1)}>上一页</button>
                  <span>第 {page + 1} / {pageCount} 页</span>
                  <button type="button" disabled={busy || page + 1 >= pageCount} onClick={() => setGroupPage(page + 1)}>下一页</button>
                </div>
              ) : <span />}
              <div className="dialog-actions">
                <button type="button" disabled={busy} onClick={() => setExpandedGroup(null)}>关闭</button>
              </div>
            </div>
          </section>
        </div>
      ) : null}
      {adding ? (
        <div className="inventory-add-form">
          <label>构件编号<input value={newEntry.component_number} onChange={(event) => setNewEntry((current) => ({ ...current, component_number: event.target.value }))} /></label>
          <label>构件类别<input value={newEntry.site_component_type} onChange={(event) => setNewEntry((current) => ({ ...current, site_component_type: event.target.value, site_name: event.target.value }))} /></label>
          <label>所属跨或位置<input value={newEntry.span_or_location ?? ""} onChange={(event) => setNewEntry((current) => ({ ...current, span_or_location: event.target.value }))} /></label>
          <button type="button" disabled={busy || !newEntry.component_number.trim() || !newEntry.site_component_type.trim() || !newEntry.site_name.trim()} onClick={() => void addEntry()}>添加到草稿</button>
          <button type="button" disabled={busy} onClick={() => setAdding(false)}>取消</button>
        </div>
      ) : null}
      <div className="inventory-panel-actions">
        <button type="button" disabled={busy} onClick={() => setAdding(true)}>＋ 手动添加构件</button>
        <button type="button" className="is-primary-action" disabled={busy || revision.status === "已确认" || blockerTotal > 0} onClick={() => void mutate(() => confirmComponentInventory(backendBaseUrl, revision.id))}>
          {busy ? "正在处理…" : "确认本版台账"}
        </button>
      </div>
    </section>
  );
}
