import type { RatingTreeNodeSummary } from "../api/ratingTreeApi";

type RatingTreeLabelNode = Pick<
  RatingTreeNodeSummary,
  "display_name" | "display_number" | "node_key" | "node_type"
>;

type RatingTreeOptionNode = RatingTreeLabelNode & Pick<
  RatingTreeNodeSummary,
  "id" | "parent_node_id" | "path" | "sort_order"
>;

const organizationDefectNumberOverrides: Readonly<Record<string, string>> = {
  // Keep the published node key stable while showing its organization-tree leaf number.
  "9_1_2": "9.1.2-1",
};

function numericPath(value: string): string[] | null {
  const parts = value.split("_");
  return parts.length > 0 && parts.every((part) => /^\d+$/.test(part))
    ? parts
    : null;
}

export function ratingTreeSectionNumber(
  node: Pick<RatingTreeNodeSummary, "display_number" | "node_key" | "node_type">,
): string | null {
  if (node.display_number) return node.display_number;
  if (node.node_type === "root") return null;

  const sharedReferenceAt = node.node_key.lastIndexOf("__");
  const rawNumber = sharedReferenceAt >= 0
    ? node.node_key.slice(sharedReferenceAt + 2)
    : node.node_key.match(/^org\.bridge\.(?:group|defect|placeholder)\.(.+)$/)?.[1];
  if (rawNumber === undefined) return null;

  if (node.node_type === "defect") {
    const overriddenNumber = organizationDefectNumberOverrides[rawNumber];
    if (overriddenNumber !== undefined) return overriddenNumber;
  }

  const parts = numericPath(rawNumber);
  if (parts === null) return null;
  if (node.node_type === "defect" && parts.length > 1) {
    return `${parts.slice(0, -1).join(".")}-${parts[parts.length - 1]}`;
  }
  return parts.join(".");
}

export function ratingTreeDisplayLabel(
  node: RatingTreeLabelNode,
): string {
  const sectionNumber = ratingTreeSectionNumber(node);
  return sectionNumber === null
    ? node.display_name
    : `${sectionNumber} ${node.display_name}`;
}

function numericDisplayPath(node: RatingTreeLabelNode): number[] | null {
  const sectionNumber = ratingTreeSectionNumber(node);
  if (sectionNumber === null || !/^\d+(?:[.-]\d+)*$/.test(sectionNumber)) return null;
  return sectionNumber.split(/[.-]/).map(Number);
}

export function compareRatingTreeNodes(
  left: RatingTreeOptionNode,
  right: RatingTreeOptionNode,
): number {
  const leftPath = numericDisplayPath(left);
  const rightPath = numericDisplayPath(right);
  if (leftPath !== null && rightPath !== null) {
    const sharedLength = Math.min(leftPath.length, rightPath.length);
    for (let index = 0; index < sharedLength; index += 1) {
      if (leftPath[index] !== rightPath[index]) return leftPath[index] - rightPath[index];
    }
    if (leftPath.length !== rightPath.length) return leftPath.length - rightPath.length;
  } else if (leftPath !== null) {
    return -1;
  } else if (rightPath !== null) {
    return 1;
  }

  return left.sort_order - right.sort_order ||
    left.display_name.localeCompare(right.display_name, "zh-CN") ||
    left.id.localeCompare(right.id);
}

export function sortRatingTreeNodes<T extends RatingTreeOptionNode>(nodes: readonly T[]): T[] {
  return [...nodes].sort(compareRatingTreeNodes);
}

export function ratingTreeOptionLabel(
  node: RatingTreeOptionNode,
  options: readonly RatingTreeOptionNode[],
): string {
  const baseLabel = ratingTreeDisplayLabel(node);
  const hasDuplicateName = options.some(
    (other) => other.id !== node.id && other.display_name.trim() === node.display_name.trim(),
  );
  if (!hasDuplicateName) return baseLabel;

  const parent = node.path?.find((item) => item.id === node.parent_node_id);
  return parent === undefined
    ? baseLabel
    : `${baseLabel}（所属：${ratingTreeDisplayLabel(parent)}）`;
}
