import { useRef, useState, type Dispatch, type FormEvent } from "react";

import { componentInventoryErrorMessage, fetchLatestComponentInventory, type ComponentInventoryEntry, type ComponentInventoryRevision, type StructurePart as InventoryStructurePart } from "../../api/componentInventoryApi";
import type { BridgeAnnualInspectionData, DefectCandidate } from "../../contracts/annualInspection";
import type { ReviewDraftAction } from "../reviewDraft";
import { DefectPhotoGroup } from "./DefectPhotoGroup";
import { UnlinkedPhotosPanel } from "./UnlinkedPhotosPanel";

interface DefectsSectionProps {
  draft: BridgeAnnualInspectionData;
  importRecordId: string;
  baseUrl: string;
  bridgeId: string;
  selectedCandidateId: string | null;
  onSelect: (candidateId: string) => void;
  dispatch: Dispatch<ReviewDraftAction>;
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
  defectType: string;
  defectDescription: string;
  defectScale: string;
}

const EMPTY_MANUAL_DEFECT: ManualDefectFormState = {
  componentEntryId: "",
  defectLocation: "",
  defectType: "",
  defectDescription: "",
  defectScale: "",
};

// 数百条病害一次性全渲染会拖垮页面（每卡十余个输入框），分页渲染并在
// 待处理跳转选中某条病害时自动翻到它所在页。
const DEFECT_PAGE_SIZE = 50;

// 禁用策略改为逐控件（DefectPhotoGroup / UnlinkedPhotosPanel 内部处理），
// 不再用 fieldset disabled 一揽子禁用——那样会连"查看照片"等只读动作一起杀掉。
export function DefectsSection({ draft, importRecordId, baseUrl, bridgeId, selectedCandidateId, selectedPhotoCandidateId, onSelect, dispatch, disabled = false, allowStructureChanges = false, componentInventory = null, isDefectEditable }: DefectsSectionProps) {
  const [showAddForm, setShowAddForm] = useState(false);
  const [inventoryEntries, setInventoryEntries] = useState<ComponentInventoryEntry[]>([]);
  const [loadedInventory, setLoadedInventory] = useState<ComponentInventoryRevision | null>(null);
  const [loadingInventory, setLoadingInventory] = useState(false);
  const [formError, setFormError] = useState("");
  const [form, setForm] = useState<ManualDefectFormState>(EMPTY_MANUAL_DEFECT);
  const [page, setPage] = useState(0);
  const lastSelectedRef = useRef<string | null>(null);

  const pageCount = Math.max(1, Math.ceil(draft.defects.length / DEFECT_PAGE_SIZE));
  const currentPage = Math.min(page, pageCount - 1);
  // 选中病害变化时（待处理跳转/展开）同步翻到它所在页；渲染期 setState 让同一次提交
  // 就渲染出目标页，父层的滚动定位随后就能找到对应 DOM 锚点。
  if (selectedCandidateId && selectedCandidateId !== lastSelectedRef.current) {
    lastSelectedRef.current = selectedCandidateId;
    const index = draft.defects.findIndex((defect) => defect.candidate_id === selectedCandidateId);
    if (index >= 0) {
      const targetPage = Math.floor(index / DEFECT_PAGE_SIZE);
      if (targetPage !== currentPage) setPage(targetPage);
    }
  }
  const pageDefects = draft.defects.slice(
    currentPage * DEFECT_PAGE_SIZE,
    (currentPage + 1) * DEFECT_PAGE_SIZE
  );

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
    const location = form.defectLocation.trim();
    const type = form.defectType.trim();
    const description = form.defectDescription.trim();
    const scale = form.defectScale === "" ? null : Number(form.defectScale);
    if (!inventory || !entry || !mapping || !location || !type || !description) {
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
        defectType: type,
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
          <label>实际构件<select aria-label="实际构件" disabled={loadingInventory} required value={form.componentEntryId} onChange={(event) => setForm({ ...form, componentEntryId: event.target.value })}><option value="">请选择构件</option>{inventoryEntries.map((entry) => <option key={entry.id} value={entry.id}>{entry.component_number} / {entry.site_component_type}</option>)}</select></label>
          <label>构件类别<input aria-label="新增病害构件类别" readOnly value={inventoryEntries.find((entry) => entry.id === form.componentEntryId)?.site_component_type ?? ""} /></label>
          <label>构件编号<input aria-label="新增病害构件编号" readOnly value={inventoryEntries.find((entry) => entry.id === form.componentEntryId)?.component_number ?? ""} /></label>
          <label>病害位置<input aria-label="新增病害位置" required value={form.defectLocation} onChange={(event) => setForm({ ...form, defectLocation: event.target.value })} /></label>
          <label>病害类型<input aria-label="新增病害类型" required value={form.defectType} onChange={(event) => setForm({ ...form, defectType: event.target.value })} /></label>
          <label className="manual-defect-form-wide">病害描述<input aria-label="新增病害描述" required value={form.defectDescription} onChange={(event) => setForm({ ...form, defectDescription: event.target.value })} /></label>
          <label>病害标度（可稍后填写）<input aria-label="新增病害标度" type="number" min={1} step={1} value={form.defectScale} onChange={(event) => setForm({ ...form, defectScale: event.target.value })} /></label>
          {formError ? <p className="form-error" role="alert">{formError}</p> : null}
          <div className="manual-defect-form-actions"><button type="button" onClick={() => { setShowAddForm(false); setFormError(""); }}>取消</button><button type="submit" disabled={loadingInventory || inventoryEntries.length === 0}>添加病害</button></div>
        </form>
      ) : null}
      <fieldset className="review-disabled-fieldset">
        {draft.defects.length === 0 ? <p>暂无病害候选，可使用“新增病害”手动添加。</p> : null}
        <div className="table-scroll">
          {/* 每条病害是一张自带标签的表单卡片（DefectPhotoGroup），不再需要共享表头。 */}
          <table className="data-table defect-photo-table">
          {pageDefects.map((defect, index) => (
              <DefectPhotoGroup
                key={defect.candidate_id}
                draft={draft}
                defect={defect}
                sequenceNumber={currentPage * DEFECT_PAGE_SIZE + index + 1}
                importRecordId={importRecordId}
                baseUrl={baseUrl}
                expanded={selectedCandidateId === defect.candidate_id}
                initialPhotoCandidateId={selectedPhotoCandidateId}
                onToggle={() => onSelect(defect.candidate_id)}
                dispatch={dispatch}
                disabled={disabled || (isDefectEditable !== undefined && !isDefectEditable(defect))}
                allowDelete={allowStructureChanges}
                componentInventory={componentInventory ?? loadedInventory}
              />
            ))}
          </table>
        </div>
        {pageCount > 1 ? (
          <div className="defect-pagination">
            <button type="button" disabled={currentPage === 0} onClick={() => setPage(currentPage - 1)}>上一页</button>
            <span>第 {currentPage + 1} / {pageCount} 页（共 {draft.defects.length} 条病害）</span>
            <button type="button" disabled={currentPage + 1 >= pageCount} onClick={() => setPage(currentPage + 1)}>下一页</button>
          </div>
        ) : null}
        <UnlinkedPhotosPanel draft={draft} importRecordId={importRecordId} baseUrl={baseUrl} selectedPhotoCandidateId={selectedPhotoCandidateId} dispatch={dispatch} disabled={disabled} />
      </fieldset>
    </section>
  );
}
