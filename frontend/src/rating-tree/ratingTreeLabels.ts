import type { RatingTreeNodeSummary } from "../api/ratingTreeApi";

function numericPath(value: string): string[] | null {
  const parts = value.split("_");
  return parts.length > 0 && parts.every((part) => /^\d+$/.test(part))
    ? parts
    : null;
}

export function ratingTreeSectionNumber(
  node: Pick<RatingTreeNodeSummary, "node_key" | "node_type">,
): string | null {
  if (node.node_type === "root") return null;

  const sharedReferenceAt = node.node_key.lastIndexOf("__");
  const rawNumber = sharedReferenceAt >= 0
    ? node.node_key.slice(sharedReferenceAt + 2)
    : node.node_key.match(/^org\.bridge\.(?:group|defect|placeholder)\.(.+)$/)?.[1];
  if (rawNumber === undefined) return null;

  const parts = numericPath(rawNumber);
  if (parts === null) return null;
  if (node.node_type === "defect" && parts.length > 1) {
    return `${parts.slice(0, -1).join(".")}-${parts[parts.length - 1]}`;
  }
  return parts.join(".");
}

export function ratingTreeDisplayLabel(
  node: Pick<RatingTreeNodeSummary, "display_name" | "node_key" | "node_type">,
): string {
  const sectionNumber = ratingTreeSectionNumber(node);
  return sectionNumber === null
    ? node.display_name
    : `${sectionNumber}、${node.display_name}`;
}
