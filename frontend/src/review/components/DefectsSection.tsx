import { useEffect, useMemo, useRef, useState, type Dispatch, type FormEvent } from "react";

import type { AssessmentIssue } from "../../api/assessmentApi";
import { componentInventoryErrorMessage, fetchLatestComponentInventory, type ComponentInventoryEntry, type ComponentInventoryRevision, type StructurePart as InventoryStructurePart } from "../../api/componentInventoryApi";
import { fetchStandardCatalog, standardsErrorMessage, type StandardDefectCatalog } from "../../api/standardsApi";
import type { BridgeAnnualInspectionData, DefectCandidate } from "../../contracts/annualInspection";
import { buildDefectPhotoReviewModel, type DefectReviewFilter, type DefectReviewProblemCategory } from "../defectPhotoReviewModel";
import type { ReviewDraftAction } from "../reviewDraft";
import { DefectBatchConfirmDialog } from "./DefectBatchConfirmDialog";
import { DefectDetailEditor } from "./DefectDetailEditor";
import { DefectQuickReviewList } from "./DefectQuickReviewList";
import { DefectReviewToolbar } from "./DefectReviewToolbar";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";

interface DefectsSectionProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  bridgeId: string;
  selectedCandidateId: string | null;
  onSelect: (candidateId: string, photoCandidateId?: string) => void;
  dispatch: Dispatch<ReviewDraftAction>;
  technicalStandardPackageId?: string | null;
  assessmentIssues?: AssessmentIssue[];
  selectedPhotoCandidateId?: string | null;
  disabled?: boolean;
  allowStructureChanges?: boolean;
  componentInventory?: ComponentInventoryRevision | null;
  /**
   * 逐病害可编辑判定（重开校对 warnings_only 态下仅带警告的病害可改）。
   * 不传视为全部可编辑；与 disabled 叠加：disabled=true 时全部不可编辑。
   */
  isDefectEditable?: (defect: DefectCandidate) => boolean;
}

const standardCatalogCache = new Map<string, StandardDefectCatalog[]>();
const EMPTY_ASSESSMENT_ISSUES: AssessmentIssue[] = [];

const STRUCTURE_PART_LABELS: Record<InventoryStructurePart, "全桥" | "上部结构" | "下部结构" | "桥面系" | "其他"> = {
  overall: "全桥",
  superstructure: "上部结构",
  substructure: "下部结构",
  deck_system: "桥面系",
  other: "其他",
};

interface ManualDefectFormState {
  componentEntryId: string;
  defectLocation: string;
  standardDefectIndicatorId: string;
  defectDescription: string;
  defectScale: string;
}

const EMPTY_MANUAL_DEFECT: ManualDefectFormState = {
  componentEntryId: "",
  defectLocation: "",
  standardDefectIndicatorId: "",
  defectDescription: "",
  defectScale: "",
};

// 禁用策略按详情控件处理，不用 fieldset disabled 一揽子禁用；
// 筛选、翻页、缩略图等只读动作在已确认记录中仍可使用。
export function DefectsSection({ draft, importRecordId, baseUrl, bridgeId, selectedCandidateId, selectedPhotoCandidateId, onSelect, dispatch, technicalStandardPackageId = null, assessmentIssues = EMPTY_ASSESSMENT_ISSUES, disabled = false, allowStructureChanges = false, componentInventory = null, isDefectEditable }: DefectsSectionProps) {
  const [showAddForm, setShowAddForm] = useState(false);
  const [inventoryEntries, setInventoryEntries] = useState<ComponentInventoryEntry[]>([]);
  const [loadedInventory, setLoadedInventory] = useState<ComponentInventoryRevision | null>(null);
  const [loadingInventory, setLoadingInventory] = useState(false);
  const [formError, setFormError] = useState("");
  const [form, setForm] = useState<ManualDefectFormState>(EMPTY_MANUAL_DEFECT);
  const [catalogs, setCatalogs] = useState<StandardDefectCatalog[]>(
    technicalStandardPackageId ? standardCatalogCache.get(technicalStandardPackageId) ?? [] : [],
  );
  const [catalogError, setCatalogError] = useState("");
  const [filter, setFilter] = useState<DefectReviewFilter>("needs_attention");
  const [problemCategory, setProblemCategory] = useState<DefectReviewProblemCategory | null>(null);
  const [search, setSearch] = useState("");
  const [selectedIds, setSelectedIds] = useState<Set<string>>(new Set());
  const [batchDialogOpen, setBatchDialogOpen] = useState(false);
  const seenSafeIds = useRef(new Set<string>());
  const appliedSuggestionIds = useRef(new Set<string>());

  useEffect(() => {
    if (!technicalStandardPackageId) {
      setCatalogs([]);
      return;
    }
    const cached = standardCatalogCache.get(technicalStandardPackageId);
    if (cached) {
      setCatalogs(cached);
      return;
    }
    let cancelled = false;
    setCatalogError("");
    fetchStandardCatalog(baseUrl, technicalStandardPackageId)
      .then((catalog) => {
        if (cancelled) return;
        standardCatalogCache.set(technicalStandardPackageId, catalog.defect_catalogs);
        setCatalogs(catalog.defect_catalogs);
      })
      .catch((error) => {
        if (!cancelled) setCatalogError(standardsErrorMessage(error));
      });
    return () => { cancelled = true; };
  }, [baseUrl, technicalStandardPackageId]);

  const allModel = useMemo(() => buildDefectPhotoReviewModel({
    draft,
    defectCatalogs: catalogs,
    assessmentIssues,
  }), [assessmentIssues, catalogs, draft]);
  const visibleModel = useMemo(() => buildDefectPhotoReviewModel({
    draft,
    defectCatalogs: catalogs,
    assessmentIssues,
    filter,
    problemCategory,
    search,
  }), [assessmentIssues, catalogs, draft, filter, problemCategory, search]);

  useEffect(() => {
    if (catalogs.length === 0) return;
    const matches = allModel.rows
      .filter((row) => row.suggestedIndicator && !appliedSuggestionIds.current.has(row.candidateId))
      .map((row) => {
        appliedSuggestionIds.current.add(row.candidateId);
        return {
          candidateId: row.candidateId,
          indicatorId: row.suggestedIndicator!.id,
          indicatorName: row.suggestedIndicator!.name,
        };
      });
    if (matches.length > 0) dispatch({ type: "apply_unique_standard_indicator_matches", matches });
  }, [allModel.rows, catalogs.length, dispatch]);

  useEffect(() => {
    setSelectedIds((current) => {
      const next = new Set([...current].filter((id) => allModel.safeCandidateIds.has(id)));
      for (const id of allModel.safeCandidateIds) {
        if (!seenSafeIds.current.has(id)) next.add(id);
        seenSafeIds.current.add(id);
      }
      if (next.size === current.size && [...next].every((id) => current.has(id))) return current;
      return next;
    });
  }, [allModel.safeCandidateIds]);

  const currentRow = selectedCandidateId
    ? allModel.rows.find((row) => row.candidateId === selectedCandidateId) ?? null
    : null;
  const currentlySafeSelection = [...selectedIds].filter((id) => allModel.safeCandidateIds.has(id));
  const selectedPhotoCount = draft.photos.filter(
    (photo) => photo.linked_defect_candidate_id && currentlySafeSelection.includes(photo.linked_defect_candidate_id),
  ).length;
  const selectedEntry = inventoryEntries.find((entry) => entry.id === form.componentEntryId);
  const selectedMapping = selectedEntry?.mappings.find((item) => item.is_active);
  const manualDefectIndicators = selectedMapping
    ? catalogs
        .filter((catalog) => catalog.applicable_component_ids.includes(selectedMapping.standard_component_category_id))
        .flatMap((catalog) => catalog.indicators)
    : [];

  const openAddForm = async () => {
    setShowAddForm(true);
    setFormError("");
    if (componentInventory) {
      const usable = componentInventory.entries.filter((entry) => entry.is_active && entry.mappings.some((mapping) => mapping.is_active));
      setLoadedInventory(componentInventory);
      setInventoryEntries(usable);
      setForm((current) => ({ ...current, componentEntryId: current.componentEntryId || usable[0]?.id || "" }));
      if (usable.length === 0) setFormError("当前构件台账中没有可用于关联病害的有效构件。");
      return;
    }
    setLoadingInventory(true);
    try {
      const revision = await fetchLatestComponentInventory(baseUrl, bridgeId);
      setLoadedInventory(revision);
      const usable = revision.entries.filter((entry) => entry.is_active && entry.mappings.some((mapping) => mapping.is_active));
      setInventoryEntries(usable);
      setForm((current) => ({ ...current, componentEntryId: current.componentEntryId || usable[0]?.id || "" }));
      if (usable.length === 0) setFormError("当前构件台账中没有可用于关联病害的有效构件。");
    } catch (error) {
      setInventoryEntries([]);
      setFormError(componentInventoryErrorMessage(error));
    } finally {
      setLoadingInventory(false);
    }
  };

  const submitManualDefect = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const entry = inventoryEntries.find((item) => item.id === form.componentEntryId);
    const inventory = loadedInventory ?? componentInventory;
    const mapping = entry?.mappings.find((item) => item.is_active);
    const indicator = manualDefectIndicators.find((item) => item.id === form.standardDefectIndicatorId);
    const location = form.defectLocation.trim();
    const description = form.defectDescription.trim();
    const scale = form.defectScale === "" ? null : Number(form.defectScale);
    if (!inventory || !entry || !mapping || !indicator || !location || !description) {
      setFormError("请填写构件类别、构件编号、病害位置、病害类型和病害描述。");
      return;
    }
    if (scale !== null && (!Number.isInteger(scale) || scale <= 0)) {
      setFormError("病害标度必须是正整数，也可以暂时留空。");
      return;
    }
    dispatch({
      type: "add_defect",
      input: {
        componentName: entry.site_component_type,
        componentNumber: entry.component_number,
        bridgeComponentId: entry.bridge_component_id,
        standardComponentCategoryId: mapping.standard_component_category_id,
        resolvedStructurePart: STRUCTURE_PART_LABELS[mapping.structure_part],
        inventoryRevisionId: inventory.id,
        defectLocation: location,
        defectType: indicator.name,
        standardDefectIndicatorId: indicator.id,
        defectDescription: description,
        defectScale: scale,
      },
    });
    setForm(EMPTY_MANUAL_DEFECT);
    setShowAddForm(false);
    setFormError("");
  };

  return (
    <section className="status-panel defect-photo-section">
      <div className="defect-section-heading">
        <h2>病害与照片</h2>
        <button type="button" disabled={!allowStructureChanges || loadingInventory} onClick={openAddForm}>新增病害</button>
      </div>
      {showAddForm ? (
        <form className="manual-defect-form" onSubmit={submitManualDefect}>
          <label>实际构件<select aria-label="实际构件" disabled={loadingInventory} required value={form.componentEntryId} onChange={(event) => setForm({ ...form, componentEntryId: event.target.value, standardDefectIndicatorId: "" })}><option value="">请选择构件</option>{inventoryEntries.map((entry) => <option key={entry.id} value={entry.id}>{entry.component_number} / {entry.site_component_type}</option>)}</select></label>
          <label>构件类别<input aria-label="新增病害构件类别" readOnly value={inventoryEntries.find((entry) => entry.id === form.componentEntryId)?.site_component_type ?? ""} /></label>
          <label>构件编号<input aria-label="新增病害构件编号" readOnly value={inventoryEntries.find((entry) => entry.id === form.componentEntryId)?.component_number ?? ""} /></label>
          <label>病害位置<input aria-label="新增病害位置" required value={form.defectLocation} onChange={(event) => setForm({ ...form, defectLocation: event.target.value })} /></label>
          <label>病害类型<select aria-label="新增病害类型" required value={form.standardDefectIndicatorId} onChange={(event) => setForm({ ...form, standardDefectIndicatorId: event.target.value })}><option value="">请选择规范病害</option>{manualDefectIndicators.map((indicator) => <option key={indicator.id} value={indicator.id}>{indicator.name}</option>)}</select></label>
          <label className="manual-defect-form-wide">病害描述<input aria-label="新增病害描述" required value={form.defectDescription} onChange={(event) => setForm({ ...form, defectDescription: event.target.value })} /></label>
          <label>病害标度（可稍后填写）<input aria-label="新增病害标度" type="number" min={1} step={1} value={form.defectScale} onChange={(event) => setForm({ ...form, defectScale: event.target.value })} /></label>
          {formError ? <p className="form-error" role="alert">{formError}</p> : null}
          <div className="manual-defect-form-actions"><button type="button" onClick={() => { setShowAddForm(false); setFormError(""); }}>取消</button><button type="submit" disabled={loadingInventory || inventoryEntries.length === 0}>添加病害</button></div>
        </form>
      ) : null}
      <fieldset className="review-disabled-fieldset">
        {draft.defects.length === 0 ? <p>暂无病害候选，可使用“新增病害”手动添加。</p> : null}
        {catalogError ? <p className="form-error" role="alert">{catalogError}</p> : null}
        {!technicalStandardPackageId ? <p className="warning-text">当前检测年度未配置技术评定规范，无法确定规范病害。</p> : null}
        <DefectReviewToolbar
          summary={allModel.summary}
          filter={filter}
          problemCategory={problemCategory}
          search={search}
          selectedCount={currentlySafeSelection.length}
          disabled={disabled}
          onFilterChange={setFilter}
          onProblemCategoryChange={setProblemCategory}
          onSearchChange={setSearch}
          onBatchConfirm={() => setBatchDialogOpen(true)}
        />
        <DefectQuickReviewList
          rows={visibleModel.rows}
          importRecordId={importRecordId}
          baseUrl={baseUrl}
          selectedCandidateIds={selectedIds}
          activeCandidateId={selectedCandidateId}
          onToggleSelection={(candidateId) => setSelectedIds((current) => {
            const next = new Set(current);
            if (next.has(candidateId)) next.delete(candidateId);
            else if (allModel.safeCandidateIds.has(candidateId)) next.add(candidateId);
            return next;
          })}
          onOpen={onSelect}
        />
        {currentRow ? (
          <DefectDetailEditor
            draft={draft}
            row={currentRow}
            catalogs={catalogs}
            componentInventory={componentInventory ?? loadedInventory}
            importRecordId={importRecordId}
            baseUrl={baseUrl}
            initialPhotoCandidateId={selectedPhotoCandidateId}
            dispatch={dispatch}
            disabled={disabled || (isDefectEditable !== undefined && !isDefectEditable(currentRow.defect))}
            allowDelete={allowStructureChanges}
            onConfirmAndNext={() => {
              dispatch({ type: "confirm_defect_groups", candidateIds: [currentRow.candidateId] });
              const next = allModel.rows.find(
                (row) => row.candidateId !== currentRow.candidateId && row.status === "needs_attention",
              );
              if (next) onSelect(next.candidateId);
            }}
          />
        ) : <p className="defect-detail-placeholder">选择一条病害后可精细维护档案和照片关系。</p>}
        <UnlinkedPhotosPanel draft={draft} importRecordId={importRecordId} baseUrl={baseUrl} selectedPhotoCandidateId={selectedPhotoCandidateId} dispatch={dispatch} disabled={disabled} />
      </fieldset>
      <DefectBatchConfirmDialog
        open={batchDialogOpen}
        defectCount={currentlySafeSelection.length}
        photoCount={selectedPhotoCount}
        removedCount={selectedIds.size - currentlySafeSelection.length}
        onCancel={() => setBatchDialogOpen(false)}
        onConfirm={() => {
          const validIds = [...selectedIds].filter((id) => allModel.safeCandidateIds.has(id));
          if (validIds.length > 0) dispatch({ type: "confirm_defect_groups", candidateIds: validIds });
          setBatchDialogOpen(false);
        }}
      />
    </section>
  );
}
