import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { EditOutlined, MoreOutlined, PlusOutlined } from "@ant-design/icons";
import {
  Alert,
  Button,
  Card,
  Col,
  Divider,
  Drawer,
  Dropdown,
  Flex,
  Form,
  Grid,
  Input,
  Menu,
  Modal,
  Pagination,
  Popover,
  Row,
  Select,
  Space,
  Table,
  Tag,
  Typography,
  theme,
  type MenuProps,
  type TableProps,
} from "antd";

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
import { StatusTag } from "../workspace/StatusTag";
import { InventoryPlanPanel } from "./InventoryPlanPanel";
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
const kDefaultEntriesPageSize = 20;
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
  const [blockersOpen, setBlockersOpen] = useState(false);
  const [newEntry, setNewEntry] = useState<InventoryEntryInput>({
    component_number: "", site_name: "", site_component_type: "", span_or_location: "", remarks: "",
  });
  const [expandedGroup, setExpandedGroup] = useState<string | null>(null);
  const [groupPage, setGroupPage] = useState(0);
  const [groupPageSize, setGroupPageSize] = useState(kDefaultEntriesPageSize);
  const [search, setSearch] = useState("");
  const [structureFilter, setStructureFilter] = useState("全部部位");
  const [categoryFilter, setCategoryFilter] = useState("全部类别");
  const [mappingFilter, setMappingFilter] = useState("全部状态");
  // 一次只编辑一行。构件动辄上千个，绝大多数只是被翻阅，不该整页都摆成输入框。
  const [editingEntryId, setEditingEntryId] = useState<string | null>(null);
  // 停用原因只在真要停用时才问，不再每行常驻一个空输入框。
  const [deactivatingId, setDeactivatingId] = useState<string | null>(null);
  const [pendingFocusId, setPendingFocusId] = useState<string | null>(null);
  // 定位某一行时按行键在表格里找它：antd 表格的行元素不经过我们手里的 ref。
  const tableRef = useRef<HTMLDivElement | null>(null);
  const { token } = theme.useToken();
  const stacked = Grid.useBreakpoint().lg === false;

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
      backendBaseUrl, revision.id, expandedGroup, groupPage, groupPageSize, controller.signal)
      .then((next) => {
        if (controller.signal.aborted) return;
        setGroupEntries(next);
        // 删构件后该组可能变短甚至清空：按新 total 夹取页码，归零则关掉弹窗。
        if (next.total === 0) { setExpandedGroup(null); return; }
        const lastPage = Math.max(0, Math.ceil(next.total / groupPageSize) - 1);
        if (groupPage > lastPage) setGroupPage(lastPage);
      })
      .catch((caught) => {
        if (controller.signal.aborted) return;
        setError(componentInventoryErrorMessage(caught));
      })
      .finally(() => { if (!controller.signal.aborted) setGroupEntriesLoading(false); });
    return () => controller.abort();
  }, [revision?.id, expandedGroup, groupPage, groupPageSize, refreshToken]);

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
  const visibleGroupSections = useMemo(
    () => groupSections
      .map((section) => ({
        ...section,
        groups: section.groups.filter((group) => {
          if (structureFilter !== "全部部位" && structurePartLabel(section.key) !== structureFilter) return false;
          if (categoryFilter !== "全部类别" && group.siteComponentType !== categoryFilter) return false;
          if (mappingFilter === "已映射" && (group.unmappedCount > 0 || group.pendingCount > 0)) return false;
          if (mappingFilter === "待核对" && group.pendingCount === 0 && group.unmappedCount === 0) return false;
          return true;
        }),
      }))
      .filter((section) => section.groups.length > 0),
    [categoryFilter, groupSections, mappingFilter, structureFilter],
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
  const expandedGroupSummary = useMemo(
    () => groupSummaries.find((group) => group.siteComponentType === expandedGroup) ?? null,
    [groupSummaries, expandedGroup]
  );
  const groupTotal = groupEntries?.total ?? 0;
  const pageCount = Math.max(1, Math.ceil(groupTotal / groupPageSize));
  const page = Math.min(groupPage, pageCount - 1);
  const pageEntries = groupEntries?.entries ?? [];

  useEffect(() => {
    const firstVisible = visibleGroupSections[0]?.groups[0]?.siteComponentType ?? null;
    const currentVisible = visibleGroupSections.some((section) =>
      section.groups.some((group) => group.siteComponentType === expandedGroup));
    if (expandedGroup && currentVisible) return;
    setExpandedGroup(firstVisible);
    setGroupPage(0);
  }, [expandedGroup, visibleGroupSections]);

  useEffect(() => {
    if (!pendingFocusId) return;
    const row = tableRef.current?.querySelector<HTMLTableRowElement>(`tr[data-row-key="${pendingFocusId}"]`);
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
    setGroupPage(Math.floor(sample.position / groupPageSize));
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

  function mappingLabelForEntry(entry: ComponentInventoryEntry) {
    const activeMapping = entry.mappings.find((item) => item.is_active);
    if (!activeMapping) return "—";
    const catalog = catalogs.find((item) => item.package.id === activeMapping.standard_package_id)
      ?? catalogs.find((item) => item.component_categories.some(
        (category) => category.id === activeMapping.standard_component_category_id
      ));
    const category = catalog?.component_categories.find(
      (item) => item.id === activeMapping.standard_component_category_id
    );
    return `${catalog?.package.standard_code ?? "技术评定规范"} · ${
      category?.name ?? activeMapping.standard_component_category_id
    }`;
  }

  /* 只读是常态、编辑是例外。主列表按档案台账的阅读顺序固定为六列；编辑入口保留为
     行级次要动作，避免与更常用的“查看档案”争夺视觉焦点。编辑中的那一行整行让给编辑区。 */
  function entryMappingState(entry: ComponentInventoryEntry) {
    /* 一个构件可以按规范包挂多个生效映射（唯一索引是 entry + package）。状态要按
       "存在任一已确认"判断，和汇总、confirm 是同一套口径；只看首个映射的话，
       [待确认(包A)、已确认(包B)] 会在一个"全部已确认"的分组里显示成待确认，
       还允许再确认一次 A。 */
    const activeMappings = entry.mappings.filter((item) => item.is_active);
    return {
      activeMapping: activeMappings[0],
      hasConfirmedMapping: activeMappings.some((item) => item.confirmation_status === "已确认"),
    };
  }

  function renderEntryEditor(entry: ComponentInventoryEntry) {
    if (!revision) return null;
    const draft = drafts[entry.id] ?? entryDraft(entry);
    const { activeMapping, hasConfirmedMapping } = entryMappingState(entry);
    const mappingDraft = mappingDrafts[entry.id];
    const mappingCatalog = catalogs.find((item) => item.package.id === mappingDraft?.packageId);
    const mappingCategories = mappingCatalog?.component_categories.filter(
      (item) => item.bridge_type_ids.includes(mappingDraft?.bridgeTypeId ?? "")
    ) ?? [];
    const dirty = draftIsDirty(entry, draft);
    const deactivating = deactivatingId === entry.id;
    const reasonText = deactivationReasons[entry.id] ?? "";
    const moreActions: MenuProps["items"] = [
      // 已确认的映射整组一致、分组核对表已经显示，这里只在需要处理时才出现。
      ...(activeMapping && !hasConfirmedMapping
        ? [{ key: "confirm-mapping", label: "确认映射", onClick: () => void confirmExistingMapping(entry) }]
        : []),
      ...(!activeMapping && !mappingDraft
        ? [{ key: "set-mapping", label: "设置规范映射", disabled: catalogs.length === 0, onClick: () => beginMapping(entry.id) }]
        : []),
      ...(entry.is_active && entry.is_referenced
        ? [{ key: "deactivate", label: "停用", onClick: () => setDeactivatingId(entry.id) }]
        : []),
      ...(entry.is_active && !entry.is_referenced
        ? [{
            key: "delete",
            label: "删除",
            danger: true,
            onClick: () => void mutate(() => deleteComponentInventoryEntry(backendBaseUrl, revision.id, entry.id)),
          }]
        : []),
    ];
    return (
      <Flex vertical gap={10}>
        <Flex align="end" justify="space-between" gap={12} wrap>
          <Flex align="end" gap={12} wrap>
            <Flex vertical gap={4} component="label">
              <Typography.Text type="secondary">构件编号</Typography.Text>
              <Input aria-label={`构件编号 ${entry.component_number}`} value={draft.component_number} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, component_number: event.target.value } }))} />
            </Flex>
            {/* 现场名称与构件类别在生成时就是同一个值，页面不再单列，改类别时同步跟随。 */}
            <Flex vertical gap={4} component="label">
              <Typography.Text type="secondary">构件类别</Typography.Text>
              <Input aria-label={`构件类别 ${entry.component_number}`} value={draft.site_component_type} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, site_component_type: event.target.value, site_name: event.target.value } }))} />
            </Flex>
            <Flex vertical gap={4} component="label">
              <Typography.Text type="secondary">所属跨或位置</Typography.Text>
              <Input aria-label={`所属跨或位置 ${entry.component_number}`} value={draft.span_or_location ?? ""} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, span_or_location: event.target.value } }))} />
            </Flex>
          </Flex>
          <Flex gap={8}>
            <Button type="primary" disabled={busy || !entry.is_active || !dirty} onClick={() => void saveEntry(entry)}>保存</Button>
            <Button disabled={busy} onClick={cancelEdit}>取消</Button>
            {/* 低频且不可逆的动作收进菜单，不和保存抢视觉权重。 */}
            <Dropdown trigger={["click"]} disabled={busy} menu={{ items: moreActions }}>
              <Button icon={<MoreOutlined />} aria-label={`更多操作 ${entry.component_number}`}>更多</Button>
            </Dropdown>
          </Flex>
        </Flex>

        {!activeMapping && mappingDraft ? (
          <Flex gap={8} wrap>
            <Select
              aria-label={`映射规范 ${entry.component_number}`}
              value={mappingDraft.packageId}
              style={{ width: 180 }}
              onChange={(packageId) => {
                const nextCatalog = catalogs.find((item) => item.package.id === packageId);
                updateMappingDraft(entry.id, { packageId, bridgeTypeId: nextCatalog?.bridge_types[0]?.id ?? "", categoryId: "" });
              }}
              options={catalogs.map((item) => ({ value: item.package.id, label: item.package.standard_code }))}
            />
            <Select
              aria-label={`映射桥型 ${entry.component_number}`}
              value={mappingDraft.bridgeTypeId}
              style={{ width: 160 }}
              onChange={(bridgeTypeId) => updateMappingDraft(entry.id, { bridgeTypeId, categoryId: "" })}
              options={mappingCatalog?.bridge_types.map((item) => ({ value: item.id, label: item.name }))}
            />
            <Select
              aria-label={`映射类别 ${entry.component_number}`}
              placeholder="请选择类别"
              value={mappingDraft.categoryId || undefined}
              style={{ width: 200 }}
              onChange={(categoryId) => updateMappingDraft(entry.id, { categoryId })}
              options={mappingCategories.map((item) => ({ value: item.id, label: item.name }))}
            />
            <Button disabled={busy || !mappingDraft.categoryId} onClick={() => void saveMapping(entry.id)}>保存映射</Button>
          </Flex>
        ) : null}

        {deactivating ? (
          <>
            <Divider dashed size="small" />
            <Flex align="center" gap={8} wrap>
              <Input
                aria-label={`停用原因 ${entry.component_number}`}
                placeholder="停用原因"
                value={reasonText}
                style={{ width: 288 }}
                onChange={(event) => setDeactivationReasons((current) => ({ ...current, [entry.id]: event.target.value }))}
              />
              <Button danger disabled={busy || !reasonText.trim()} onClick={() => void mutate(() => deactivateComponentInventoryEntry(backendBaseUrl, revision.id, entry.id, reasonText.trim()))}>确认停用</Button>
              <Button disabled={busy} onClick={() => setDeactivatingId(null)}>取消停用</Button>
            </Flex>
          </>
        ) : null}
      </Flex>
    );
  }

  // 编辑中的那一行：第一格横跨六列放编辑区，其余格让位。
  const editingCell = (entry: ComponentInventoryEntry) => ({
    colSpan: editingEntryId === entry.id ? 6 : 1,
    style: editingEntryId === entry.id ? { background: token.colorPrimaryBg } : undefined,
  });
  const yieldToEditor = (entry: ComponentInventoryEntry) => ({ colSpan: editingEntryId === entry.id ? 0 : 1 });

  const entryColumns: TableProps<ComponentInventoryEntry>["columns"] = [
    {
      title: "构件编号",
      key: "component_number",
      width: "13%",
      onCell: editingCell,
      render: (_, entry) => (editingEntryId === entry.id ? renderEntryEditor(entry) : entry.component_number),
    },
    { title: "现场名称", key: "site_name", width: "18%", onCell: yieldToEditor, render: (_, entry) => entry.site_name || "—" },
    { title: "构件类别", key: "site_component_type", width: "14%", onCell: yieldToEditor, render: (_, entry) => entry.site_component_type || "—" },
    { title: "规范映射", key: "mapping", width: "34%", onCell: yieldToEditor, render: (_, entry) => mappingLabelForEntry(entry) },
    {
      title: "状态",
      key: "state",
      width: "10%",
      onCell: yieldToEditor,
      render: (_, entry) => {
        const { activeMapping, hasConfirmedMapping } = entryMappingState(entry);
        if (!entry.is_active) return <Tag>已停用</Tag>;
        if (!activeMapping) return <Tag color="warning">无映射</Tag>;
        if (!hasConfirmedMapping) return <Tag color="warning">待确认</Tag>;
        return <Tag color="success">已映射</Tag>;
      },
    },
    {
      title: "操作",
      key: "actions",
      width: "11%",
      align: "right",
      onCell: yieldToEditor,
      render: (_, entry) => (
        <Flex align="center" justify="end" gap={4}>
          <Typography.Link href={`/bridges/${bridgeId}/components/${entry.bridge_component_id}`}>
            查看档案
          </Typography.Link>
          <Button
            type="text"
            size="small"
            icon={<EditOutlined />}
            aria-label="编辑"
            title="编辑构件"
            disabled={busy}
            onClick={() => beginEdit(entry)}
          />
        </Flex>
      ),
    },
  ];

  const heading = <Typography.Title level={3} style={{ margin: 0 }}>实际构件台账</Typography.Title>;

  if (loading || (revision !== null && catalogLoading)) {
    return (
      <Card>
        <Flex vertical gap={8}>
          {heading}
          <Typography.Text type="secondary">正在加载台账与规范映射…</Typography.Text>
        </Flex>
      </Card>
    );
  }

  if (notCreated) {
    return (
      <Card>
        <Flex vertical gap={12}>
          {heading}
          <Typography.Text type="secondary">这座桥还没有构件台账。填写数量后，系统会生成每一个实际构件编号。</Typography.Text>
          <InventoryPlanPanel onPlanChange={setPlan} />
          {error ? <Alert type="error" showIcon title={error} /> : null}
          <Flex gap={8}>
            <Button type="primary" loading={busy} disabled={!plan} onClick={() => void generate()}>
              生成初始构件台账
            </Button>
          </Flex>
        </Flex>
      </Card>
    );
  }

  if (!revision) {
    return (
      <Card>
        <Flex vertical gap={12} align="start">
          {heading}
          <Alert type="error" showIcon title={error ?? "加载构件台账失败。"} />
          <Button onClick={() => void load()}>重试</Button>
        </Flex>
      </Card>
    );
  }

  /*
   * 台账要一屏放下：卡片高度吃满顶栏以下的视口，标题行、工具栏、底部提示都是固定的一行，
   * 剩下的高度全给左右两栏，两栏各自滚动。142px = 顶栏 68 + 内容区上下留白 30 与 44。
   * 窄屏上下叠放时交还给整页滚动。
   */
  const ledgerHeight = stacked ? undefined : "max(560px, calc(100dvh - 142px))";
  const listTitle = searchTerm ? "搜索结果" : "构件列表";
  const confirmedCount = Math.max(0, revision.active_entry_count - (summary?.blockers.individual_total ?? 0));
  const stats = [
    { title: "构件总数", value: revision.active_entry_count, color: token.colorPrimary },
    { title: "构件类别", value: groupSummaries.length, color: token.colorPrimary },
    { title: "已映射", value: confirmedCount, color: token.colorSuccess },
    { title: "待核对", value: blockerTotal, color: token.colorWarning },
  ];

  return (
    <Card
      style={{ height: ledgerHeight }}
      styles={{
        root: { display: "flex", flexDirection: "column" },
        body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column", gap: 14 },
      }}
    >
      <Flex align="center" justify="space-between" gap={16} wrap>
        <Flex vertical gap={2}>
          <Typography.Text type="secondary">版本 {revision.revision_number}</Typography.Text>
          <Flex align="center" gap={12}>
            {heading}
            <StatusTag status={inventoryStatus(revision.status)} />
          </Flex>
        </Flex>

        {/* 四个数原本单占一张卡片，并进标题行，给下面的列表腾出一行高度。 */}
        <Flex align="center" gap={20} wrap role="group" aria-label="构件台账概况">
          {stats.map((item, index) => (
            <Flex key={item.title} align="center" gap={20}>
              {index > 0 ? <Divider vertical /> : null}
              <Flex vertical>
                <Typography.Text type="secondary">{item.title}</Typography.Text>
                <Typography.Title level={4} style={{ margin: 0, color: item.color }}>
                  {item.value.toLocaleString()}
                </Typography.Title>
              </Flex>
            </Flex>
          ))}
        </Flex>

        <Flex align="center" justify="end" gap={8} wrap>
          <Popover
            trigger="click"
            placement="bottomRight"
            content={
              <Flex vertical gap={6}>
                <Typography.Text>构件编号和现场名称可修改，修改编号不会改变构件身份。</Typography.Text>
                {revision.status === "已确认" ? <Typography.Text>修改已确认台账时，系统会自动创建下一版草稿。</Typography.Text> : null}
              </Flex>
            }
          >
            <Button>版本说明</Button>
          </Popover>
          <Button icon={<PlusOutlined />} disabled={busy} onClick={() => setAdding(true)}>手动添加构件</Button>
          {revision.status !== "已确认" ? <Button type="primary" disabled={busy || blockerTotal > 0} onClick={() => void mutate(() => confirmComponentInventory(backendBaseUrl, revision.id))}>确认本版台账</Button> : null}
        </Flex>
      </Flex>

      <Row gutter={[12, 12]}>
        <Col xs={24} lg={8}>
          <Input aria-label="搜索构件" placeholder="搜索编号、类别或现场名" allowClear value={search} onChange={(event) => setSearch(event.target.value)} />
        </Col>
        <Col xs={12} lg={4}>
          <Select
            aria-label="按部位筛选"
            value={structureFilter}
            style={{ width: "100%" }}
            onChange={setStructureFilter}
            options={["全部部位", ...groupSections.map((section) => structurePartLabel(section.key))].map((value) => ({ value, label: value }))}
          />
        </Col>
        <Col xs={12} lg={4}>
          <Select
            aria-label="按类别筛选"
            value={categoryFilter}
            style={{ width: "100%" }}
            onChange={setCategoryFilter}
            options={["全部类别", ...groupSummaries.map((group) => group.siteComponentType)].map((value) => ({ value, label: value }))}
          />
        </Col>
        <Col xs={12} lg={4}>
          <Select
            aria-label="按映射状态筛选"
            value={mappingFilter}
            style={{ width: "100%" }}
            onChange={setMappingFilter}
            options={["全部状态", "已映射", "待核对"].map((value) => ({ value, label: value }))}
          />
        </Col>
        <Col xs={12} lg={4}>
          <Button block onClick={() => { setSearch(""); setStructureFilter("全部部位"); setCategoryFilter("全部类别"); setMappingFilter("全部状态"); }}>重置</Button>
        </Col>
      </Row>

      {catalogError ? (
        <Alert type="warning" showIcon role="status" title={`规范映射名称暂时取不到（${catalogError}）；构件与编号不受影响，可稍后重试。`} />
      ) : null}
      {error ? <Alert type="error" showIcon title={error} closable onClose={() => setError(null)} /> : null}

      <Row gutter={[14, 14]} style={stacked ? undefined : { flex: 1, minHeight: 0 }}>
        <Col xs={24} lg={6} style={stacked ? undefined : { height: "100%" }}>
          <Card
            size="small"
            title="构件分类"
            style={{ height: stacked ? undefined : "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, minHeight: 0, maxHeight: stacked ? 280 : undefined, overflowY: "auto", padding: 0 },
            }}
          >
            <Menu
              mode="inline"
              selectedKeys={expandedGroup ? [expandedGroup] : []}
              onClick={({ key }) => openGroup(key)}
              items={visibleGroupSections.map((section) => ({
                type: "group" as const,
                key: `section:${section.key}`,
                label: (
                  <Flex justify="space-between" gap={8}>
                    <span>{structurePartLabel(section.key)}</span>
                    <span>{section.groups.reduce((total, group) => total + group.activeCount, 0).toLocaleString()}</span>
                  </Flex>
                ),
                children: section.groups.map((group) => ({
                  key: group.siteComponentType,
                  label: (
                    <Flex justify="space-between" gap={8}>
                      <Typography.Text ellipsis>{group.siteComponentType}</Typography.Text>
                      <Typography.Text type="secondary">{group.activeCount.toLocaleString()}</Typography.Text>
                    </Flex>
                  ),
                })),
              }))}
            />
          </Card>
        </Col>

        <Col xs={24} lg={18} style={stacked ? undefined : { height: "100%" }}>
          <Card
            size="small"
            role="region"
            aria-label={listTitle}
            title={<Typography.Title level={5} style={{ margin: 0 }}>{listTitle}</Typography.Title>}
            extra={
              <Flex align="center" gap={10}>
                <Typography.Text type="secondary">
                  {searchTerm ? `匹配 ${searchMatches.length} 条` : `共 ${groupTotal.toLocaleString()} 条`}
                </Typography.Text>
                {!searchTerm && expandedGroup && (expandedGroupSummary?.pendingCount ?? 0) > 0 ? (
                  <Button size="small" disabled={busy} onClick={() => void confirmPendingMappings(expandedGroup)}>确认该组映射</Button>
                ) : null}
              </Flex>
            }
            style={{ height: stacked ? 560 : "100%" }}
            styles={{
              root: { display: "flex", flexDirection: "column" },
              body: { flex: 1, minHeight: 0, display: "flex", flexDirection: "column", padding: 0 },
            }}
          >
            <div ref={tableRef} style={{ flex: 1, minHeight: 0, overflow: "auto" }}>
              <Table<ComponentInventoryEntry>
                rowKey="id"
                size="small"
                tableLayout="fixed"
                columns={entryColumns}
                dataSource={searchTerm ? searchMatches.slice(0, kMaxSearchResults) : pageEntries}
                loading={!searchTerm && groupEntriesLoading}
                pagination={false}
                scroll={{ x: 920 }}
                onRow={(entry) => ({
                  tabIndex: -1,
                  style: !entry.is_active && editingEntryId !== entry.id ? { opacity: 0.62 } : undefined,
                })}
              />
            </div>
            {!searchTerm ? (
              <Flex justify="end" style={{ padding: "11px 14px", borderTop: `1px solid ${token.colorSplit}` }}>
                <Pagination
                  current={page + 1}
                  pageSize={groupPageSize}
                  total={groupTotal}
                  pageSizeOptions={[20, 50, 100]}
                  showSizeChanger
                  showQuickJumper
                  showTotal={(total) => `共 ${total.toLocaleString()} 条`}
                  disabled={busy}
                  size="small"
                  onChange={(nextPage, nextPageSize) => {
                    setGroupPageSize(nextPageSize);
                    setGroupPage(nextPage - 1);
                  }}
                />
              </Flex>
            ) : null}
          </Card>
        </Col>
      </Row>

      {/* 确认前的待办固定一行：明细最多三十条，摊开会把列表挤出屏幕，放进抽屉里看。 */}
      {blockerTotal > 0 ? (
        <Alert
          type="warning"
          showIcon
          role="status"
          title={`确认前还需处理 ${blockerTotal} 项${pendingMappingCount > 0 ? `，其中 ${pendingMappingCount} 个构件的规范映射待确认` : ""}`}
          action={
            <Space>
              {pendingMappingCount > 0 ? (
                <Button
                  size="small"
                  disabled={busy || revision.status !== "草稿"}
                  onClick={() => void confirmPendingMappings()}
                >
                  一键确认全部待确认映射
                </Button>
              ) : null}
              {individualBlockers.length > 0 ? (
                <Button size="small" onClick={() => setBlockersOpen(true)}>查看明细</Button>
              ) : null}
            </Space>
          }
        />
      ) : null}

      <Drawer
        open={blockersOpen}
        size={480}
        title={`确认前还需处理 ${blockerTotal} 项`}
        onClose={() => setBlockersOpen(false)}
      >
        <Flex vertical gap={10}>
          {/* 样本已由服务端截断到 30 条，这里不再自己 slice。 */}
          {individualBlockers.map((blocker, index) => (
            <Flex key={`${blocker.code}-${blocker.entity_id}-${index}`} align="baseline" justify="space-between" gap={12}>
              <Typography.Text>{blocker.message}</Typography.Text>
              {blocker.entity_type === "inventory_entry" && blocker.position !== null ? (
                <Button
                  type="link"
                  size="small"
                  onClick={() => {
                    setBlockersOpen(false);
                    focusEntry(blocker);
                  }}
                >
                  定位
                </Button>
              ) : null}
            </Flex>
          ))}
          {remainingBlockers > 0 ? (
            <Typography.Text type="secondary">……其余 {remainingBlockers} 项处理后依次显示。</Typography.Text>
          ) : null}
        </Flex>
      </Drawer>

      <Modal
        open={adding}
        title="手动添加构件"
        okText="添加到草稿"
        cancelText="取消"
        confirmLoading={busy}
        okButtonProps={{ disabled: !newEntry.component_number.trim() || !newEntry.site_component_type.trim() || !newEntry.site_name.trim() }}
        onOk={() => void addEntry()}
        onCancel={() => setAdding(false)}
      >
        <Form layout="vertical">
          <Form.Item label="构件编号" htmlFor="inventory-add-number">
            <Input id="inventory-add-number" value={newEntry.component_number} onChange={(event) => setNewEntry((current) => ({ ...current, component_number: event.target.value }))} />
          </Form.Item>
          {/* 现场名称与构件类别在生成时就是同一个值，改类别时同步跟随。 */}
          <Form.Item label="构件类别" htmlFor="inventory-add-type">
            <Input id="inventory-add-type" value={newEntry.site_component_type} onChange={(event) => setNewEntry((current) => ({ ...current, site_component_type: event.target.value, site_name: event.target.value }))} />
          </Form.Item>
          <Form.Item label="所属跨或位置" htmlFor="inventory-add-location" style={{ marginBottom: 0 }}>
            <Input id="inventory-add-location" value={newEntry.span_or_location ?? ""} onChange={(event) => setNewEntry((current) => ({ ...current, span_or_location: event.target.value }))} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  );
}
