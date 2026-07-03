from __future__ import annotations

import html
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path


OUT_DIR = Path("docs/superpowers/diagrams/module03")


def esc(value: str) -> str:
    return html.escape(value, quote=True)


def lines_for(text: str, width: int) -> list[str]:
    if "\n" in text:
        return text.splitlines()
    if len(text) <= width:
        return [text]
    return textwrap.wrap(text, width=width, break_long_words=False, replace_whitespace=False)


class Svg:
    def __init__(self, path: Path, width: int, height: int, title: str, subtitle: str) -> None:
        self.path = path
        self.width = width
        self.height = height
        self.out: list[str] = []
        self.out.append(
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">'
        )
        self.out.append("<style>")
        self.out.append(
            "text { font-family: 'Helvetica Neue', Helvetica, Arial, 'PingFang SC', 'Microsoft YaHei', SimHei, sans-serif; }"
        )
        self.out.append(".title { font-size: 24px; font-weight: 700; fill: #111827; }")
        self.out.append(".subtitle { font-size: 13px; fill: #64748b; }")
        self.out.append(".lane-title { font-size: 13px; font-weight: 700; fill: #334155; }")
        self.out.append(".node-title { font-size: 15px; font-weight: 700; fill: #111827; }")
        self.out.append(".node-sub { font-size: 12px; fill: #475569; }")
        self.out.append(".tiny { font-size: 11px; fill: #64748b; }")
        self.out.append(".mono { font-family: Consolas, 'Microsoft YaHei', monospace; font-size: 12px; fill: #334155; }")
        self.out.append("</style>")
        self.out.append("<defs>")
        markers = {
            "blue": "#2563eb",
            "green": "#059669",
            "orange": "#ea580c",
            "purple": "#7c3aed",
            "gray": "#64748b",
            "red": "#dc2626",
        }
        for name, color in markers.items():
            self.out.append(
                f'<marker id="{name}" markerWidth="12" markerHeight="8" refX="10" refY="4" orient="auto" markerUnits="strokeWidth">'
            )
            self.out.append(f'<path d="M 0 0 L 12 4 L 0 8 z" fill="{color}"/>')
            self.out.append("</marker>")
        self.out.append("</defs>")
        self.out.append(f'<rect width="{width}" height="{height}" fill="#f8fafc"/>')
        self.out.append(f'<text x="48" y="46" class="title">{esc(title)}</text>')
        self.out.append(f'<text x="48" y="70" class="subtitle">{esc(subtitle)}</text>')

    def lane(self, x: int, y: int, w: int, h: int, title: str, fill: str = "#ffffff") -> None:
        self.out.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="16" fill="{fill}" stroke="#e2e8f0" stroke-width="1.2"/>'
        )
        self.out.append(f'<text x="{x + 18}" y="{y + 28}" class="lane-title">{esc(title)}</text>')

    def node(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        title: str,
        subtitle: str = "",
        fill: str = "#ffffff",
        stroke: str = "#cbd5e1",
        badge: str | None = None,
    ) -> None:
        self.out.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fill}" stroke="{stroke}" stroke-width="1.4"/>'
        )
        if badge:
            self.out.append(f'<circle cx="{x + 24}" cy="{y + 25}" r="13" fill="{stroke}"/>')
            self.out.append(f'<text x="{x + 24}" y="{y + 30}" text-anchor="middle" font-size="12" font-weight="700" fill="#ffffff">{esc(badge)}</text>')
            title_x = x + 48
            anchor = "start"
        else:
            title_x = x + w / 2
            anchor = "middle"
        title_lines = lines_for(title, max(8, (w - 28) // 12))[:2]
        title_start_y = y + 29
        if h <= 64 and subtitle:
            title_start_y = y + 25
        for i, line in enumerate(title_lines):
            self.out.append(
                f'<text x="{title_x}" y="{title_start_y + i * 17}" text-anchor="{anchor}" class="node-title">{esc(line)}</text>'
            )
        if subtitle:
            if h <= 64:
                start_y = y + h - 12
                max_lines = 1
            elif h <= 82:
                start_y = y + 54
                max_lines = 2
            else:
                start_y = y + 56 if badge else y + 58
                max_lines = 4
            for i, line in enumerate(lines_for(subtitle, max(10, (w - 28) // 7))[:max_lines]):
                self.out.append(f'<text x="{x + 16}" y="{start_y + i * 15}" class="node-sub">{esc(line)}</text>')

    def pill(self, x: int, y: int, text: str, fill: str = "#eef2ff", stroke: str = "#c7d2fe") -> None:
        w = max(76, len(text) * 11 + 22)
        self.out.append(f'<rect x="{x}" y="{y}" width="{w}" height="28" rx="14" fill="{fill}" stroke="{stroke}"/>')
        self.out.append(f'<text x="{x + w / 2}" y="{y + 19}" text-anchor="middle" class="tiny" font-weight="700">{esc(text)}</text>')

    def arrow(
        self,
        points: list[tuple[int, int]],
        color: str = "blue",
        label: str | None = None,
        dashed: bool = False,
        width: float = 2.0,
        via: list[tuple[int, int]] | None = None,
    ) -> None:
        colors = {
            "blue": "#2563eb",
            "green": "#059669",
            "orange": "#ea580c",
            "purple": "#7c3aed",
            "gray": "#64748b",
            "red": "#dc2626",
        }
        dash = ' stroke-dasharray="6 5"' if dashed else ""
        routed_points = [points[0], *(via or []), *points[1:]]
        d = "M " + " L ".join(f"{x},{y}" for x, y in routed_points)
        self.out.append(
            f'<path d="{d}" fill="none" stroke="{colors[color]}" stroke-width="{width}" marker-end="url(#{color})"{dash}/>'
        )
        if label:
            px, py = routed_points[len(routed_points) // 2]
            label_w = max(72, len(label) * 12 + 22)
            self.out.append(
                f'<rect x="{px - label_w / 2:.1f}" y="{py - 28}" width="{label_w}" height="22" rx="11" fill="#ffffff" stroke="#e2e8f0"/>'
            )
            self.out.append(f'<text x="{px}" y="{py - 12}" text-anchor="middle" class="tiny">{esc(label)}</text>')

    def legend(self, x: int, y: int, items: list[tuple[str, str]]) -> None:
        colors = {
            "blue": "#2563eb",
            "green": "#059669",
            "orange": "#ea580c",
            "purple": "#7c3aed",
            "gray": "#64748b",
            "red": "#dc2626",
        }
        h = 30 + 22 * len(items)
        self.out.append(f'<rect x="{x}" y="{y}" width="260" height="{h}" rx="12" fill="#ffffff" stroke="#e2e8f0"/>')
        self.out.append(f'<text x="{x + 14}" y="{y + 22}" class="tiny" font-weight="700">图例</text>')
        for i, (color, label) in enumerate(items):
            yy = y + 46 + i * 22
            self.out.append(
                f'<line x1="{x + 14}" y1="{yy}" x2="{x + 56}" y2="{yy}" stroke="{colors[color]}" stroke-width="2" marker-end="url(#{color})"/>'
            )
            self.out.append(f'<text x="{x + 66}" y="{yy + 4}" class="tiny">{esc(label)}</text>')

    def note(self, x: int, y: int, w: int, text: str) -> None:
        self.out.append(f'<rect x="{x}" y="{y}" width="{w}" height="58" rx="12" fill="#fffbeb" stroke="#f59e0b"/>')
        for i, line in enumerate(lines_for(text, max(12, (w - 28) // 7))[:3]):
            self.out.append(f'<text x="{x + 16}" y="{y + 23 + i * 15}" class="node-sub">{esc(line)}</text>')

    def save(self) -> None:
        self.out.append("</svg>")
        self.path.write_text("\n".join(self.out), encoding="utf-8")


def draw_overview() -> None:
    s = Svg(
        OUT_DIR / "03-01-overview-bridge.svg",
        1440,
        820,
        "模块 03 总览：候选数据契约是中间桥梁",
        "从原始文件到正式事实表，中间统一使用 BridgeAnnualInspectionData",
    )
    lanes = [
        (48, "输入来源", "#eff6ff"),
        (322, "Python 工具服务", "#faf5ff"),
        (596, "03 契约层", "#f0fdf4"),
        (870, "C++ 与前端", "#fff7ed"),
        (1144, "正式事实", "#ffffff"),
    ]
    for x, title, fill in lanes:
        s.lane(x, 104, 226, 560, title, fill)
    s.node(76, 160, 170, 84, "今年软件 Word", "病害表、照片、第四章评分", "#ffffff", "#60a5fa", "1")
    s.node(76, 278, 170, 84, "上一年正式 Word", "可选历史基线", "#ffffff", "#60a5fa", "2")
    s.node(76, 396, 170, 84, "未来来源", "Excel / API / JSON 先预留", "#ffffff", "#94a3b8", "3")
    s.node(350, 176, 170, 92, "Importer 适配器", "按来源读取、提取、归一化", "#ffffff", "#c084fc")
    s.node(350, 340, 170, 92, "解析输出", "不写数据库事实，只输出候选 JSON", "#ffffff", "#c084fc")
    s.node(624, 150, 170, 96, "BridgeAnnualInspectionData", "候选数据的统一结构", "#ffffff", "#34d399")
    s.node(624, 294, 170, 96, "来源与置信度", "source_ref / confidence / warnings", "#ffffff", "#34d399")
    s.node(624, 438, 170, 96, "校对状态", "待确认 / 已确认 / 驳回", "#ffffff", "#34d399")
    s.node(898, 170, 170, 86, "import_records", "parsed_result_json 保存候选", "#ffffff", "#fb923c")
    s.node(898, 314, 170, 86, "校对工作台", "读取、修改、确认", "#ffffff", "#fb923c")
    s.node(898, 458, 170, 86, "确认入库逻辑", "检查状态和必填字段", "#ffffff", "#fb923c")
    s.node(1172, 180, 170, 88, "defect_observations", "年度病害事实", "#ffffff", "#94a3b8")
    s.node(1172, 326, 170, 88, "defect_photos / ratings", "照片和评分事实", "#ffffff", "#94a3b8")
    s.node(1172, 472, 170, 88, "defect_comparisons", "年度间对比关系", "#ffffff", "#94a3b8")
    s.arrow([(246, 202), (350, 202)], "blue", "文件")
    s.arrow([(246, 320), (304, 320), (304, 202), (350, 202)], "blue")
    s.arrow([(520, 386), (572, 386), (572, 198), (624, 198)], "green", "候选")
    s.arrow([(794, 198), (898, 213)], "green", "保存")
    s.arrow([(983, 256), (983, 314)], "blue", "读取")
    s.arrow([(983, 400), (983, 458)], "orange", "确认")
    s.arrow([(1068, 501), (1172, 224)], "orange", "写入", via=[(1118, 501), (1118, 224)])
    s.arrow([(1068, 501), (1172, 370)], "orange", via=[(1118, 501), (1118, 370)])
    s.arrow([(1068, 501), (1172, 516)], "orange", via=[(1118, 501), (1118, 516)])
    s.note(58, 700, 560, "03 不负责解析算法和页面实现，它只定义候选 JSON 长什么样。")
    s.legend(1110, 682, [("blue", "文件/读取"), ("green", "候选数据"), ("orange", "人工确认后写入")])
    s.save()


def draw_structure() -> None:
    s = Svg(
        OUT_DIR / "03-02-contract-structure.svg",
        1440,
        960,
        "BridgeAnnualInspectionData：候选 JSON 结构",
        "字段不是简单值，关键候选字段都要带来源、置信度和校对状态",
    )
    s.node(520, 110, 400, 82, "BridgeAnnualInspectionData", "contract + source + candidates + diagnostics", "#ffffff", "#34d399")
    s.lane(64, 250, 1280, 170, "基础上下文", "#eef2ff")
    s.lane(64, 462, 1280, 210, "核心候选数据", "#f0fdf4")
    s.lane(64, 720, 1280, 140, "诊断与原文追溯", "#fff7ed")
    top = [
        (110, "contract", "版本、生成时间、解析器"),
        (360, "source_files", "原始文件、抽取文件"),
        (610, "bridge_check", "桥梁校验，不覆盖档案"),
        (860, "inspection", "年度、报告编号、检测日期"),
        (1110, "review_summary", "待确认、错误、警告统计"),
    ]
    for x, title, sub in top:
        s.node(x, 306, 190, 74, title, sub, "#ffffff", "#818cf8")
        s.arrow([(720, 192), (720, 236), (x + 95, 236), (x + 95, 306)], "green", dashed=True)
    mid = [
        (110, "components[]", "构件候选、别名、结构部位"),
        (360, "defects[]", "病害类型、位置、描述"),
        (610, "measurements[]", "长宽高、面积、数量"),
        (860, "photos[]", "照片编号、图片候选、匹配状态"),
        (1110, "ratings[]", "全桥/部位/构件评分"),
    ]
    for x, title, sub in mid:
        s.node(x, 526, 190, 78, title.replace("[]", " 列表"), sub, "#ffffff", "#34d399")
    s.node(170, 762, 230, 68, "warnings 列表", "可继续校对的问题", "#ffffff", "#fb923c")
    s.node(462, 762, 230, 68, "errors 列表", "阻止确认的问题", "#ffffff", "#fb923c")
    s.node(754, 762, 230, 68, "raw_extracts 列表", "原始表格行、段落、单元格 JSON", "#ffffff", "#fb923c")
    s.node(1046, 762, 230, 68, "field wrapper", "value / source_ref / confidence / review", "#ffffff", "#fb923c")
    s.note(1038, 882, 314, "JSON key 可以用英文；业务枚举和展示值建议使用中文。")
    s.save()


def draw_sequence() -> None:
    s = Svg(
        OUT_DIR / "03-03-parse-review-sequence.svg",
        1440,
        900,
        "导入到确认入库：模块 03 交接时序",
        "解析阶段只保存候选；确认阶段才写正式业务表",
    )
    actors = [
        (110, "用户"),
        (310, "前端"),
        (520, "C++ 主服务"),
        (735, "文件归档"),
        (950, "Python 工具服务"),
        (1190, "PostgreSQL"),
    ]
    for x, label in actors:
        s.node(x - 74, 102, 148, 46, label, "", "#ffffff", "#cbd5e1")
        s.out.append(f'<line x1="{x}" y1="154" x2="{x}" y2="810" stroke="#cbd5e1" stroke-width="1.2" stroke-dasharray="6 5"/>')
    s.lane(54, 176, 1270, 306, "解析阶段：产生候选 JSON", "#f0fdf4")
    s.lane(54, 514, 1270, 290, "校对阶段：人工确认后写正式表", "#fff7ed")
    messages = [
        (220, 110, 310, "选择桥梁/年度", "blue"),
        (260, 310, 520, "上传 Word", "blue"),
        (300, 520, 735, "归档原始文件", "green"),
        (340, 520, 950, "请求解析", "orange"),
        (380, 950, 950, "抽取表格/图片", "purple"),
        (420, 950, 520, "候选 JSON", "green"),
        (460, 520, 1190, "写 parsed_result_json", "green"),
        (560, 310, 520, "打开校对页", "blue"),
        (600, 520, 1190, "读取候选 JSON", "blue"),
        (646, 310, 110, "展示待确认项", "blue"),
        (700, 110, 310, "人工修正/确认", "orange"),
        (742, 310, 520, "提交确认结果", "orange"),
        (782, 520, 1190, "写正式表", "orange"),
    ]
    for y, x1, x2, label, color in messages:
        if x1 == x2:
            s.arrow([(x1, y), (x1 + 70, y), (x1 + 70, y + 28), (x1, y + 28)], color, label)
        else:
            s.arrow([(x1, y), (x2, y)], color, label)
    s.legend(1040, 820, [("blue", "用户/读取"), ("green", "候选写入"), ("orange", "触发/确认"), ("purple", "解析处理")])
    s.save()


def draw_state() -> None:
    s = Svg(
        OUT_DIR / "03-04-candidate-state-flow.svg",
        1280,
        780,
        "候选数据状态流",
        "每条候选记录从机器抽取到正式入库的生命周期",
    )
    states = {
        "extracted": (80, 150, "已抽取", "机器生成候选值", "#eff6ff", "#60a5fa"),
        "pending": (335, 150, "待确认", "默认进入校对队列", "#ffffff", "#cbd5e1"),
        "fix": (590, 150, "需人工修正", "低置信、冲突或缺失", "#fffbeb", "#f59e0b"),
        "confirmed": (845, 150, "已确认", "满足入库条件", "#f0fdf4", "#34d399"),
        "rejected": (335, 350, "已驳回", "不进入事实库", "#fef2f2", "#f87171"),
        "need_source": (590, 350, "等待补充来源", "缺照片、页码或原文", "#fffbeb", "#f59e0b"),
        "stored": (845, 350, "已入库", "正式表生成记录", "#f0fdf4", "#34d399"),
        "revision": (590, 555, "修订候选", "同桥同年再次导入", "#faf5ff", "#c084fc"),
        "new_fact": (845, 555, "形成新版事实", "旧版标记为已修订", "#faf5ff", "#c084fc"),
    }
    for x, y, title, sub, fill, stroke in states.values():
        s.node(x, y, 175, 72, title, sub, fill, stroke)
    s.arrow([(255, 186), (335, 186)], "green", "生成")
    s.arrow([(510, 186), (590, 186)], "orange", "发现问题")
    s.arrow([(765, 186), (845, 186)], "orange", "确认")
    s.arrow([(932, 222), (932, 350)], "green", "提交")
    s.arrow([(422, 222), (422, 350)], "red", "驳回")
    s.arrow([(677, 222), (677, 350)], "orange", "来源不足")
    s.arrow([(765, 386), (845, 386)], "green", "补齐后确认")
    s.arrow([(677, 422), (677, 555)], "purple", "重复导入")
    s.arrow([(765, 591), (845, 591)], "purple", "确认修订")
    s.arrow([(590, 386), (530, 386), (530, 186), (590, 186)], "orange", "回到修正", dashed=True)
    s.note(78, 650, 570, "03 定义这些状态值；模块 5 根据状态组织校对界面，模块 2 只接收已确认的数据。")
    s.legend(865, 642, [("green", "可继续流转"), ("orange", "校对修正"), ("red", "驳回"), ("purple", "修订版本")])
    s.save()


def draw_file_photo() -> None:
    s = Svg(
        OUT_DIR / "03-05-file-photo-mapping.svg",
        1440,
        840,
        "文件、图片、照片编号引用关系",
        "区分候选 JSON 内部临时 ID 与数据库正式 GDWJ 编号",
    )
    lanes = [
        (56, "原始输入与归档", "#eff6ff"),
        (380, "Python 抽取", "#faf5ff"),
        (704, "候选 JSON", "#f0fdf4"),
        (1028, "确认后正式表", "#fff7ed"),
    ]
    for x, title, fill in lanes:
        s.lane(x, 110, 260, 570, title, fill)
    s.node(92, 176, 188, 76, "原始 Word", "用户上传文件", "#ffffff", "#60a5fa")
    s.node(92, 320, 188, 76, "archived_files", "原文件 GDWJ 编号", "#ffffff", "#60a5fa")
    s.node(92, 464, 188, 76, "import_record_files", "导入记录与文件关联", "#ffffff", "#60a5fa")
    s.node(416, 176, 188, 76, "extracted_files[]", "抽取出的图片候选", "#ffffff", "#c084fc")
    s.node(416, 320, 188, 76, "extracted_file_candidate_id", "file-cand-001", "#ffffff", "#c084fc")
    s.node(416, 464, 188, 76, "raw_extracts[]", "页码、表格、段落原文", "#ffffff", "#c084fc")
    s.node(740, 176, 188, 76, "photos[]", "照片编号候选", "#ffffff", "#34d399")
    s.node(740, 320, 188, 76, "photo_number", "例如 2.1-3", "#ffffff", "#34d399")
    s.node(740, 464, 188, 76, "defects[].photo_refs", "病害引用照片", "#ffffff", "#34d399")
    s.node(1064, 176, 188, 76, "defect_photos", "正式照片事实", "#ffffff", "#fb923c")
    s.node(1064, 320, 188, 76, "archived_files", "抽取图片 GDWJ 编号", "#ffffff", "#fb923c")
    s.node(1064, 464, 188, 76, "defect_observations", "病害观测事实", "#ffffff", "#fb923c")
    s.arrow([(186, 252), (186, 320)], "green", "归档")
    s.arrow([(280, 358), (416, 214)], "blue", "解析读取", via=[(340, 358), (340, 214)])
    s.arrow([(510, 252), (510, 320)], "purple", "生成临时 ID")
    s.arrow([(604, 358), (740, 214)], "green", "候选引用", via=[(666, 358), (666, 214)])
    s.arrow([(834, 252), (834, 320)], "purple", "识别编号")
    s.arrow([(834, 396), (834, 464)], "green", "关联病害")
    s.arrow([(928, 214), (1064, 214)], "orange", "确认后")
    s.arrow([(928, 358), (1064, 358)], "orange", "归档后")
    s.arrow([(928, 502), (1064, 502)], "orange", "确认后")
    s.note(92, 710, 590, "extracted_file_candidate_id 只在同一份候选 JSON 内稳定；GDWJ 是入库归档后的正式编号。")
    s.legend(1080, 700, [("blue", "读取原始文件"), ("green", "候选引用"), ("orange", "确认入库"), ("purple", "解析生成")])
    s.save()


def draw_table_mapping() -> None:
    s = Svg(
        OUT_DIR / "03-06-json-to-module02-tables.svg",
        1360,
        880,
        "候选 JSON 到模块 2 正式表映射",
        "03 不是数据库镜像，但必须覆盖正式入库所需字段",
    )
    s.lane(48, 120, 410, 650, "BridgeAnnualInspectionData 候选 JSON", "#f0fdf4")
    s.lane(902, 120, 410, 650, "模块 2 正式业务表", "#fff7ed")
    rows = [
        (170, "bridge_check", "桥梁校验候选", "bridges / bridge_aliases", "只校验，不自动覆盖"),
        (260, "inspection", "年度检测候选", "inspection_years", "确认后写年度事实"),
        (350, "components", "构件候选", "bridge_components", "确认后沉淀构件"),
        (440, "defects", "病害观测候选", "defect_observations", "确认后写病害事实"),
        (530, "measurements", "尺寸候选", "defect_measurements", "确认后写尺寸事实"),
        (620, "photos", "照片候选", "defect_photos", "确认后写照片事实"),
        (710, "ratings", "评分候选", "condition_ratings", "确认后写评分事实"),
    ]
    for y, left, left_sub, right, right_sub in rows:
        s.node(88, y, 320, 62, left, left_sub, "#ffffff", "#34d399")
        s.node(942, y, 320, 62, right, right_sub, "#ffffff", "#fb923c")
    s.node(548, 354, 264, 132, "C++ 确认入库逻辑", "校验契约版本、review_status、必填字段、来源引用", "#ffffff", "#60a5fa")
    merge_points = {
        "bridge_check": 384,
        "inspection": 396,
        "components": 408,
        "defects": 420,
        "measurements": 432,
        "photos": 444,
        "ratings": 456,
    }
    for y, left, _, right, __ in rows:
        color = "gray" if left == "bridge_check" else "orange"
        center_y = merge_points[left]
        s.arrow([(408, y + 31), (500, y + 31), (500, center_y), (548, center_y)], color, dashed=(left == "bridge_check"), width=1.7)
        s.arrow([(812, center_y), (860, center_y), (860, y + 31), (942, y + 31)], "green", dashed=(left == "bridge_check"), width=1.7)
    s.pill(482, 314, "已确认候选", "#fff7ed", "#fed7aa")
    s.pill(820, 314, "写正式表", "#f0fdf4", "#bbf7d0")
    s.pill(674, 514, "bridge_check 只校验", "#f8fafc", "#cbd5e1")
    s.note(500, 680, 370, "核心原则：parsed_result_json 保存候选；正式表只接收人工确认后的事实。")
    s.legend(990, 792, [("orange", "人工确认后的候选"), ("green", "写正式表"), ("gray", "只校验不覆盖")])
    s.save()


def generate() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for draw in [
        draw_overview,
        draw_structure,
        draw_sequence,
        draw_state,
        draw_file_photo,
        draw_table_mapping,
    ]:
        draw()
    for svg in sorted(OUT_DIR.glob("03-*.svg")):
        ET.parse(svg)
        print(f"valid svg: {svg}")


if __name__ == "__main__":
    generate()
