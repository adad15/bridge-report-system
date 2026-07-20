import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { ApiError } from "../api/apiClient";
import {
  addComponentInventoryEntry,
  componentInventoryErrorMessage,
  confirmComponentInventory,
  confirmPendingComponentInventoryMappings,
  deactivateComponentInventoryEntry,
  deleteComponentInventoryEntry,
  fetchLatestComponentInventory,
  generateComponentInventory,
  setComponentInventoryMapping,
  updateComponentInventoryEntry,
  type ComponentInventoryEntry,
  type ComponentInventoryRevision,
  type GenerateComponentInventoryInput,
  type InventoryBlocker,
  type InventoryEntryInput,
} from "../api/componentInventoryApi";
import {
  fetchStandardCatalog,
  fetchStandardPackages,
  type StandardCatalog,
} from "../api/standardsApi";
import { backendBaseUrl } from "../config";
import { BridgeInventoryWizard } from "./BridgeInventoryWizard";

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

export function inventoryConfirmationBlockers(revision: ComponentInventoryRevision): InventoryBlocker[] {
  const active = revision.entries.filter((entry) => entry.is_active);
  const blockers: InventoryBlocker[] = [];
  if (active.length === 0) {
    blockers.push({
      code: "inventory_empty", entity_type: "inventory_revision", entity_id: revision.id,
      field_path: "entries", message: "构件台账至少需要一个启用构件。",
    });
  }
  const seen = new Map<string, string>();
  for (const entry of revision.entries) {
    const key = `${entry.site_component_type}\u0000${entry.component_number}`;
    const duplicate = seen.get(key);
    if (duplicate) {
      blockers.push({
        code: "duplicate_component_number", entity_type: "inventory_entry", entity_id: entry.id,
        field_path: "component_number", message: `${entry.site_component_type}中存在重复编号 ${entry.component_number}。`,
      });
    } else {
      seen.set(key, entry.id);
    }
    if (entry.is_active && !entry.mappings.some((mapping) => mapping.is_active && mapping.confirmation_status === "已确认")) {
      blockers.push({
        code: "component_mapping_required", entity_type: "inventory_entry", entity_id: entry.id,
        field_path: "mappings", message: `构件 ${entry.component_number} 还没有已确认的规范映射。`,
      });
    }
  }
  return blockers;
}

function inventoryStatus(status: string) {
  return status === "已确认" ? "已确认" : status === "草稿" ? "草稿" : status;
}

export interface InventoryGroupSummary {
  siteComponentType: string;
  activeCount: number;
  firstNumber: string;
  lastNumber: string;
  mappingLabel: string;
  confirmedCount: number;
  pendingCount: number;
  unmappedCount: number;
}

export function inventoryGroupSummaries(
  revision: ComponentInventoryRevision,
  catalogs: StandardCatalog[]
): InventoryGroupSummary[] {
  const groups = new Map<string, InventoryGroupSummary>();
  for (const entry of revision.entries) {
    if (!entry.is_active) continue;
    let group = groups.get(entry.site_component_type);
    if (!group) {
      group = {
        siteComponentType: entry.site_component_type,
        activeCount: 0,
        firstNumber: entry.component_number,
        lastNumber: entry.component_number,
        mappingLabel: "",
        confirmedCount: 0,
        pendingCount: 0,
        unmappedCount: 0,
      };
      groups.set(entry.site_component_type, group);
    }
    group.activeCount += 1;
    group.lastNumber = entry.component_number;
    const mapping = entry.mappings.find((item) => item.is_active);
    if (!mapping) {
      group.unmappedCount += 1;
      continue;
    }
    if (mapping.confirmation_status === "已确认") group.confirmedCount += 1;
    else group.pendingCount += 1;
    if (!group.mappingLabel) {
      const catalog = catalogs.find((item) => item.package.id === mapping.standard_package_id);
      const category = catalog?.component_categories.find(
        (item) => item.id === mapping.standard_component_category_id
      );
      group.mappingLabel = `${catalog?.package.standard_code ?? "技术评定规范"} · ${
        category?.name ?? mapping.standard_component_category_id
      }`;
    }
  }
  return [...groups.values()];
}

function groupStatusText(group: InventoryGroupSummary): string {
  const parts: string[] = [];
  if (group.confirmedCount > 0) parts.push(`已确认 ${group.confirmedCount}`);
  if (group.pendingCount > 0) parts.push(`待确认 ${group.pendingCount}`);
  if (group.unmappedCount > 0) parts.push(`无映射 ${group.unmappedCount}`);
  return parts.join("、") || "—";
}

const kMaxIndividualBlockers = 30;

export function ComponentInventoryEditor({ bridgeId }: { bridgeId: string }) {
  const [revision, setRevision] = useState<ComponentInventoryRevision | null>(null);
  const [notCreated, setNotCreated] = useState(false);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [plan, setPlan] = useState<GenerateComponentInventoryInput | null>(null);
  const [drafts, setDrafts] = useState<Record<string, InventoryEntryInput>>({});
  const [deactivationReasons, setDeactivationReasons] = useState<Record<string, string>>({});
  const [mappingDrafts, setMappingDrafts] = useState<Record<string, MappingDraft>>({});
  const [catalogs, setCatalogs] = useState<StandardCatalog[]>([]);
  const [adding, setAdding] = useState(false);
  const [newEntry, setNewEntry] = useState<InventoryEntryInput>({
    component_number: "", site_name: "", site_component_type: "", span_or_location: "", remarks: "",
  });
  const rowRefs = useRef<Record<string, HTMLTableRowElement | null>>({});

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const latest = await fetchLatestComponentInventory(backendBaseUrl, bridgeId);
      setRevision(latest);
      setNotCreated(false);
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "component_inventory_not_found") {
        setRevision(null);
        setNotCreated(true);
      } else {
        setError(componentInventoryErrorMessage(caught));
      }
    } finally {
      setLoading(false);
    }
  }, [bridgeId]);

  useEffect(() => { void load(); }, [load]);

  useEffect(() => {
    if (!revision) return;
    setDrafts(Object.fromEntries(revision.entries.map((entry) => [entry.id, entryDraft(entry)])));
  }, [revision]);

  useEffect(() => {
    let cancelled = false;
    fetchStandardPackages(backendBaseUrl)
      .then((packages) => Promise.all(
        packages
          .filter((item) => item.family === "technical_condition" && item.is_enabled && item.sync_status === "正常")
          .map((item) => fetchStandardCatalog(backendBaseUrl, item.id))
      ))
      .then((loaded) => { if (!cancelled) setCatalogs(loaded); })
      .catch(() => { /* 台账读取与编辑不因目录展示失败而整体失效。 */ });
    return () => { cancelled = true; };
  }, []);

  const blockers = useMemo(() => revision ? inventoryConfirmationBlockers(revision) : [], [revision]);
  const groupSummaries = useMemo(
    () => (revision ? inventoryGroupSummaries(revision, catalogs) : []),
    [revision, catalogs]
  );
  const entriesById = useMemo(
    () => new Map((revision?.entries ?? []).map((entry) => [entry.id, entry])),
    [revision]
  );
  const pendingMappingCount = useMemo(
    () => groupSummaries.reduce((total, group) => total + group.pendingCount, 0),
    [groupSummaries]
  );
  const individualBlockers = useMemo(
    () =>
      blockers.filter((blocker) => {
        if (blocker.code !== "component_mapping_required") return true;
        const entry = entriesById.get(blocker.entity_id);
        return !entry?.mappings.some(
          (mapping) => mapping.is_active && mapping.confirmation_status === "待确认"
        );
      }),
    [blockers, entriesById]
  );

  async function mutate(action: () => Promise<ComponentInventoryRevision>) {
    setBusy(true);
    setError(null);
    try {
      const next = await action();
      setRevision(next);
      setNotCreated(false);
      return true;
    } catch (caught) {
      setError(componentInventoryErrorMessage(caught));
      if (caught instanceof ApiError) {
        const details = caught.details as { blockers?: InventoryBlocker[] } | undefined;
        const target = details?.blockers?.[0]?.entity_id;
        if (target) focusEntry(target);
      }
      return false;
    } finally {
      setBusy(false);
    }
  }

  function focusEntry(entryId: string) {
    const row = rowRefs.current[entryId];
    row?.scrollIntoView({ behavior: "smooth", block: "center" });
    row?.focus();
  }

  async function generate() {
    if (!plan) return;
    await mutate(() => generateComponentInventory(backendBaseUrl, bridgeId, plan));
  }

  async function saveEntry(entry: ComponentInventoryEntry) {
    const draft = drafts[entry.id];
    if (!revision || !draft) return;
    await mutate(() => updateComponentInventoryEntry(backendBaseUrl, revision.id, entry.id, draft));
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
    const mapping = entry.mappings.find((item) => item.is_active);
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

  if (loading) return <section className="workspace-card"><h1>实际构件台账</h1><p>加载中…</p></section>;

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
      <p>构件编号和现场名称可修改。内部实际构件 ID 不在页面显示，修改编号不会改变其身份。</p>
      {revision.status === "已确认" ? <p className="inventory-standard-notice">修改已确认台账时，系统会自动创建下一版草稿，原确认版本保持不变。</p> : null}
      {groupSummaries.length > 0 ? (
        <div className="inventory-group-summary">
          <h2>分组核对</h2>
          <div className="inventory-table-scroll">
            <table className="data-table">
              <thead>
                <tr><th>构件类别</th><th>数量</th><th>编号范围</th><th>规范映射</th><th>映射状态</th><th>操作</th></tr>
              </thead>
              <tbody>
                {groupSummaries.map((group) => (
                  <tr key={group.siteComponentType}>
                    <td>{group.siteComponentType}</td>
                    <td>{group.activeCount}</td>
                    <td>
                      {group.firstNumber}
                      {group.activeCount > 1 ? ` … ${group.lastNumber}` : ""}
                    </td>
                    <td>{group.mappingLabel || "—"}</td>
                    <td>{groupStatusText(group)}</td>
                    <td>
                      {group.pendingCount > 0 && revision.status === "草稿" ? (
                        <button
                          type="button"
                          disabled={busy}
                          onClick={() => void confirmPendingMappings(group.siteComponentType)}
                        >
                          确认该组映射
                        </button>
                      ) : null}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      ) : null}
      {blockers.length > 0 ? (
        <div className="inventory-blockers" role="status">
          <strong>确认前还需处理 {blockers.length} 项</strong>
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
            {individualBlockers.slice(0, kMaxIndividualBlockers).map((blocker, index) => (
              <li key={`${blocker.code}-${blocker.entity_id}-${index}`}>
                {blocker.message}
                {blocker.entity_type === "inventory_entry" ? <button type="button" onClick={() => focusEntry(blocker.entity_id)}>定位</button> : null}
              </li>
            ))}
            {individualBlockers.length > kMaxIndividualBlockers ? (
              <li>……其余 {individualBlockers.length - kMaxIndividualBlockers} 项处理后依次显示。</li>
            ) : null}
          </ul>
        </div>
      ) : (
        <p className="inventory-confirmation-summary">共 {revision.entries.filter((entry) => entry.is_active).length} 个启用构件；编号无重复；规范映射均已确认。</p>
      )}
      {error ? <p className="error-text" role="alert">{error}</p> : null}
      <div className="inventory-table-scroll">
        <table className="data-table component-inventory-table">
          <thead><tr><th>构件编号</th><th>构件类别</th><th>现场名称</th><th>所属跨或位置</th><th>规范映射</th><th>操作</th></tr></thead>
          <tbody>
            {revision.entries.map((entry) => {
              const draft = drafts[entry.id] ?? entryDraft(entry);
              const activeMapping = entry.mappings.find((item) => item.is_active);
              const activeMappingCatalog = catalogs.find((item) => item.package.id === activeMapping?.standard_package_id);
              const activeMappingCategory = activeMappingCatalog?.component_categories.find(
                (item) => item.id === activeMapping?.standard_component_category_id
              );
              const mappingDraft = mappingDrafts[entry.id];
              const mappingCatalog = catalogs.find((item) => item.package.id === mappingDraft?.packageId);
              const mappingCategories = mappingCatalog?.component_categories.filter(
                (item) => item.bridge_type_ids.includes(mappingDraft?.bridgeTypeId ?? "")
              ) ?? [];
              return (
                <tr
                  key={entry.id}
                  ref={(node) => { rowRefs.current[entry.id] = node; }}
                  tabIndex={-1}
                  className={!entry.is_active ? "inventory-entry-inactive" : undefined}
                >
                  <td><input aria-label={`构件编号 ${entry.component_number}`} value={draft.component_number} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, component_number: event.target.value } }))} /></td>
                  <td><input aria-label={`构件类别 ${entry.component_number}`} value={draft.site_component_type} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, site_component_type: event.target.value } }))} /></td>
                  <td><input aria-label={`现场名称 ${entry.component_number}`} value={draft.site_name} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, site_name: event.target.value } }))} /></td>
                  <td><input aria-label={`所属跨或位置 ${entry.component_number}`} value={draft.span_or_location ?? ""} onChange={(event) => setDrafts((current) => ({ ...current, [entry.id]: { ...draft, span_or_location: event.target.value } }))} /></td>
                  <td>
                    {activeMapping ? (
                      <div className="inventory-mapping-state">
                        <span>
                          {activeMappingCatalog?.package.standard_code ?? "技术评定规范"} · {activeMappingCategory?.name ?? "构件类别"} · {activeMapping.confirmation_status}
                        </span>
                        {activeMapping.confirmation_status !== "已确认" ? <button type="button" disabled={busy} onClick={() => void confirmExistingMapping(entry)}>确认映射</button> : null}
                      </div>
                    ) : mappingDraft ? (
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
                    ) : (
                      <button type="button" disabled={busy || catalogs.length === 0} onClick={() => beginMapping(entry.id)}>设置规范映射</button>
                    )}
                  </td>
                  <td>
                    <div className="inventory-row-actions">
                      <button type="button" disabled={busy || !entry.is_active} onClick={() => void saveEntry(entry)}>保存</button>
                      {entry.is_active && !entry.is_referenced ? (
                        <button type="button" className="danger-button" disabled={busy} onClick={() => revision && void mutate(() => deleteComponentInventoryEntry(backendBaseUrl, revision.id, entry.id))}>删除</button>
                      ) : null}
                      {entry.is_active && entry.is_referenced ? (
                        <>
                          <input aria-label={`停用原因 ${entry.component_number}`} placeholder="停用原因" value={deactivationReasons[entry.id] ?? ""} onChange={(event) => setDeactivationReasons((current) => ({ ...current, [entry.id]: event.target.value }))} />
                          <button type="button" disabled={busy || !(deactivationReasons[entry.id] ?? "").trim()} onClick={() => revision && void mutate(() => deactivateComponentInventoryEntry(backendBaseUrl, revision.id, entry.id, deactivationReasons[entry.id].trim()))}>停用</button>
                        </>
                      ) : null}
                      {!entry.is_active ? <span>已停用</span> : null}
                    </div>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      {adding ? (
        <div className="inventory-add-form">
          <label>构件编号<input value={newEntry.component_number} onChange={(event) => setNewEntry((current) => ({ ...current, component_number: event.target.value }))} /></label>
          <label>构件类别<input value={newEntry.site_component_type} onChange={(event) => setNewEntry((current) => ({ ...current, site_component_type: event.target.value }))} /></label>
          <label>现场名称<input value={newEntry.site_name} onChange={(event) => setNewEntry((current) => ({ ...current, site_name: event.target.value }))} /></label>
          <label>所属跨或位置<input value={newEntry.span_or_location ?? ""} onChange={(event) => setNewEntry((current) => ({ ...current, span_or_location: event.target.value }))} /></label>
          <button type="button" disabled={busy || !newEntry.component_number.trim() || !newEntry.site_component_type.trim() || !newEntry.site_name.trim()} onClick={() => void addEntry()}>添加到草稿</button>
          <button type="button" disabled={busy} onClick={() => setAdding(false)}>取消</button>
        </div>
      ) : null}
      <div className="inventory-panel-actions">
        <button type="button" disabled={busy} onClick={() => setAdding(true)}>＋ 手动添加构件</button>
        <button type="button" disabled={busy || revision.status === "已确认" || blockers.length > 0} onClick={() => void mutate(() => confirmComponentInventory(backendBaseUrl, revision.id))}>
          {busy ? "正在处理…" : "确认本版台账"}
        </button>
      </div>
    </section>
  );
}
