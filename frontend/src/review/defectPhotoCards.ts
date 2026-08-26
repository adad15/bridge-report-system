import type {
  BridgeAnnualInspectionData,
  DefectCandidate,
  PhotoCandidate,
} from "../contracts/annualInspection";

/**
 * 照片卡：这条病害上实际挂着的一张照片。
 * 缺图卡：Word 原文引用了某个编号，但当前没有对应的已挂照片。
 */
export type DefectPhotoCardKind = "photo" | "missing";

export interface DefectPhotoCard {
  /** 稳定的渲染 key；照片用候选 ID，缺图用编号，两者不会撞。 */
  key: string;
  kind: DefectPhotoCardKind;
  photoNumber: string;
  /** 照片卡才有。 */
  photo: PhotoCandidate | null;
  /** 照片来自 Word 抽取还是人工上传——决定"删除"是退回未归属还是永久删除。 */
  source: "word" | "manual";
  /** 缺图卡：是否已确认"原报告就没有这张图"。 */
  acknowledgedMissing: boolean;
}

function photoSource(photo: PhotoCandidate): "word" | "manual" {
  return photo.source_ref.source_type === "manual" ? "manual" : "word";
}

/**
 * 派生一条病害的照片卡片列表（设计 §5.1）。
 *
 *   ① photos 中挂在本病害上的每一张                     → 照片卡
 *   ② photo_references 中找不到同编号已挂照片的每个编号 → 缺图卡
 *
 * 顺序：先按 Word 原文的引用顺序，再追加编号不在引用里的照片（人工补充的）。
 * 纯函数，同一份草稿重复调用结果一致。
 *
 * 这条规则让"删除"不需要额外状态：摘掉一张 Word 照片后它不再满足 ①，
 * 其编号随即满足 ②，卡片自动变回缺图卡。
 */
export function buildDefectPhotoCards(
  draft: BridgeAnnualInspectionData,
  defect: DefectCandidate,
): DefectPhotoCard[] {
  const linked = draft.photos.filter(
    (photo) => photo.linked_defect_candidate_id === defect.candidate_id,
  );
  const cards: DefectPhotoCard[] = [];
  const usedPhotoIds = new Set<string>();

  for (const reference of defect.photo_references) {
    const photo = linked.find(
      (candidate) =>
        candidate.photo_number === reference.photo_number &&
        !usedPhotoIds.has(candidate.candidate_id),
    );
    if (photo) {
      usedPhotoIds.add(photo.candidate_id);
      cards.push({
        key: photo.candidate_id,
        kind: "photo",
        photoNumber: photo.photo_number,
        photo,
        source: photoSource(photo),
        acknowledgedMissing: false,
      });
      continue;
    }
    cards.push({
      key: `reference:${reference.photo_number}`,
      kind: "missing",
      photoNumber: reference.photo_number,
      photo: null,
      source: "word",
      // relinked / unrelated 是旧模型的结论，新模型不再产生；存量草稿里的这两个值
      // 要重新回到待核对，不能当成已处理放过（设计 §9.4）。
      acknowledgedMissing: reference.resolution === "missing",
    });
  }

  for (const photo of linked) {
    if (usedPhotoIds.has(photo.candidate_id)) continue;
    cards.push({
      key: photo.candidate_id,
      kind: "photo",
      photoNumber: photo.photo_number,
      photo,
      source: photoSource(photo),
      acknowledgedMissing: false,
    });
  }

  return cards;
}
