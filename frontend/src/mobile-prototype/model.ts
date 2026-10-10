import rawCatalog from "./rating-catalog.json";

export type Indicator = {
  id: string;
  number: string;
  name: string;
  parentId: string;
  sortOrder: number;
  bridgeTypeIds: string[];
  componentCategoryIds: string[];
};

export type Measurement = {
  label: string;
  value: string;
  end: string;
  unit: string;
  approximate: boolean;
};

export type Photo = { id: string; name: string; dataUrl: string };

export type NewDefect = {
  id: string;
  componentId: string;
  indicatorId: string;
  location: string;
  direction: string;
  measurements: Measurement[];
  photos: Photo[];
  photoReason: string;
  note: string;
  createdAt: string;
};

export type Review = {
  change: string;
  repair: string;
  observation: string;
  confirmedAt: string;
};

export type SubmittedComponent = {
  inspectorId: string;
  submittedAt: string;
  revision: number;
  defects: NewDefect[];
  reviews: Record<string, Review>;
  newChecked: boolean;
};

export type TaskState = {
  defects: NewDefect[];
  reviews: Record<string, Review>;
  newChecked: Record<string, boolean>;
  completed: Record<string, boolean>;
  submissions: Record<string, SubmittedComponent>;
  syncedByInspector: Record<string, Record<string, SubmittedComponent>>;
  lastSyncedAt: Record<string, string>;
  dirty: Record<string, boolean>;
};

export type HistoryDefect = {
  id: string;
  indicatorName: string;
  location: string;
  description: string;
  year: number;
};

export type Component = {
  id: string;
  name: string;
  number: string;
  primary: "上部结构" | "下部结构" | "桥面系";
  secondary: string;
  categoryId: string;
  catalogNumber: string;
  material: string;
  history: HistoryDefect[];
};

export const bridgeName = "东河大桥";
export const inspectionYear = 2026;
export const bridgeTypeId = "h21.bridge_type.beam";

export const inspectors = [
  { id: "zhang", name: "张工", componentIds: ["girder-1-2", "crossbeam-1-3"] },
  { id: "li", name: "李工", componentIds: ["pier-2", "pavement-left"] },
  { id: "wang", name: "王工", componentIds: ["railing-left"] },
] as const;
export const ownerByComponent: Record<string, string> = Object.fromEntries(
  inspectors.flatMap((inspector) => inspector.componentIds.map((id) => [id, inspector.id]))
);
export function inspectorName(id: string): string {
  return inspectors.find((inspector) => inspector.id === id)?.name ?? "未分配";
}

export const components: Component[] = [
  {
    id: "girder-1-2",
    name: "1-2# 主梁",
    number: "1-2#",
    primary: "上部结构",
    secondary: "上部承重构件",
    categoryId: "h21.component.beam.upper_bearing",
    catalogNumber: "5.1.1",
    material: "混凝土梁",
    history: [
      {
        id: "history-girder-crack",
        indicatorName: "简支梁（板）桥、刚架桥裂缝",
        location: "第 1 跨梁底",
        description: "梁底横向裂缝，长约 0.8 m，最大宽度约 0.2 mm。",
        year: 2024,
      },
      {
        id: "history-girder-spalling",
        indicatorName: "剥落、掉角",
        location: "第 1 跨梁端",
        description: "梁端边角局部剥落，面积约 0.02 m²。",
        year: 2025,
      },
    ],
  },
  {
    id: "crossbeam-1-3",
    name: "1-3# 横隔梁",
    number: "1-3#",
    primary: "上部结构",
    secondary: "上部一般构件",
    categoryId: "h21.component.beam.upper_general",
    catalogNumber: "5.1.1",
    material: "混凝土构件",
    history: [],
  },
  {
    id: "pier-2",
    name: "2# 墩身",
    number: "2#",
    primary: "下部结构",
    secondary: "桥墩",
    categoryId: "h21.component.lower.pier",
    catalogNumber: "9.1.1",
    material: "混凝土构件",
    history: [
      {
        id: "history-pier-spalling",
        indicatorName: "剥落、露筋",
        location: "2# 墩上游侧",
        description: "上游侧局部剥落、露筋，面积约 0.3 m²。",
        year: 2025,
      },
    ],
  },
  {
    id: "pavement-left",
    name: "左幅桥面铺装",
    number: "左幅",
    primary: "桥面系",
    secondary: "桥面铺装",
    categoryId: "h21.component.deck.pavement",
    catalogNumber: "10.1.1",
    material: "沥青混凝土",
    history: [
      {
        id: "history-pavement-crack",
        indicatorName: "裂缝",
        location: "第 2 跨左幅行车道",
        description: "纵向裂缝，长约 1.6 m。",
        year: 2025,
      },
    ],
  },
  {
    id: "railing-left",
    name: "左侧护栏",
    number: "左侧",
    primary: "桥面系",
    secondary: "栏杆、护栏",
    categoryId: "h21.component.deck.railing",
    catalogNumber: "10.4.1",
    material: "混凝土护栏",
    history: [],
  },
];

const catalog = rawCatalog as {
  source: string;
  groups: Record<string, { number: string; name: string }>;
  defects: Indicator[];
};

export function indicatorsFor(component: Component): Indicator[] {
  return catalog.defects
    .filter((item) =>
      catalog.groups[item.parentId]?.number === component.catalogNumber &&
      item.bridgeTypeIds.includes(bridgeTypeId) &&
      item.componentCategoryIds.includes(component.categoryId)
    )
    .sort((a, b) => a.sortOrder - b.sortOrder);
}

export function indicatorById(id: string): Indicator | undefined {
  return catalog.defects.find((item) => item.id === id);
}

export function displayGroup(indicator: Indicator): string {
  const number = indicator.number;
  if (number.startsWith("5.1.1-")) {
    const itemNumber = Number(number.split("-")[1]);
    if (itemNumber <= 3) return "表面损伤";
    if (itemNumber <= 7) return "钢筋与材料";
    if (itemNumber <= 12) return "裂缝与变形";
    return "其他指标";
  }
  return catalog.groups[indicator.parentId]?.name ?? "病害指标";
}

export function initialMeasurements(indicator: Indicator): Measurement[] {
  if (indicator.name.includes("裂缝") || indicator.name.includes("开裂")) {
    return [
      { label: "长度", value: "", end: "", unit: "m", approximate: false },
      { label: "宽度", value: "", end: "", unit: "mm", approximate: false },
    ];
  }
  if (["剥落", "麻面", "孔洞", "破损", "露筋"].some((word) => indicator.name.includes(word))) {
    return [{ label: "面积", value: "", end: "", unit: "m²", approximate: false }];
  }
  return [{ label: "测量值", value: "", end: "", unit: "mm", approximate: false }];
}

export function blankTask(): TaskState {
  return { defects: [], reviews: {}, newChecked: {}, completed: {}, submissions: {}, syncedByInspector: {}, lastSyncedAt: {}, dirty: {} };
}

export function descriptionFor(record: NewDefect): string {
  const indicator = indicatorById(record.indicatorId);
  const parts = [record.location, record.direction, indicator?.name];
  const measures = record.measurements
    .filter((item) => item.value.trim())
    .map((item) => {
      const amount = item.end.trim() ? item.value + "～" + item.end : item.value;
      return item.label + (item.approximate ? "约 " : " ") + amount + item.unit;
    });
  if (measures.length) parts.push(measures.join("，"));
  return parts.filter(Boolean).join("，") + "。";
}

export function componentStatus(task: TaskState, component: Component): "未检查" | "复查中" | "已完成" {
  if (task.completed[component.id]) return "已完成";
  if (
    task.defects.some((item) => item.componentId === component.id) ||
    component.history.some((item) => task.reviews[item.id]) ||
    task.newChecked[component.id]
  ) return "复查中";
  return "未检查";
}

export function componentCanComplete(task: TaskState, component: Component): boolean {
  return component.history.every((item) => Boolean(task.reviews[item.id]));
}




