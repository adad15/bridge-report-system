"""一次生成的只读输入模型（设计 §9.2）。

C++ 侧的 `bridge_report::report::ReportContext` 在一次一致性读取里组装好，序列化成
JSON 发过来；这里是它在 Python 这一端的形状。契约在三种语言里各校一遍（设计 §23.3），
所以这里不是"照抄字段"，而是把 C++ 承诺过的不变量再验一次——跨语言边界上，两边都
认为对方保证了的事，正是最容易一起漏掉的事。

Builder 只认这份上下文：不连数据库，不回头查任何东西。
"""

from __future__ import annotations

from pydantic import BaseModel, ConfigDict, Field, model_validator


class ContextModel(BaseModel):
    #: 多一个字段就报错。C++ 加了字段而 Python 没跟上时要立刻炸，不能静默丢掉。
    model_config = ConfigDict(extra="forbid")


class ReportPhoto(ContextModel):
    photo_id: str
    archived_file_id: str
    #: 相对归档根目录的路径，由调用方拼成绝对路径。
    storage_relative_path: str
    #: 报告里的图号，C++ 侧按模板结构现编（设计 §7.5）。
    report_number: str
    title: str | None = None
    #: 「{图号}␠␠{标题}」，两个空格；标题为空时只有图号（设计 §11.7）。
    caption: str
    #: 系统内部编号，Word 导入路的匹配键。只作来源证据，不进报告正文（设计 §9.1）。
    source_photo_number: str | None = None

    @model_validator(mode="after")
    def check_caption_matches_number(self) -> "ReportPhoto":
        expected = self.report_number if not self.title else f"{self.report_number}  {self.title}"
        if self.caption != expected:
            raise ValueError(
                f"图题与图号对不上：图号 {self.report_number!r}，图题 {self.caption!r}"
            )
        return self


class ReportDefectRow(ContextModel):
    row_number: int
    observation_id: str
    part_name: str | None = None
    component_number: str | None = None
    defect_location: str | None = None
    defect_type: str
    description: str
    #: 标度。库里是文本列，报告照原样印，不在这里当数字解析。
    scale: str | None = None
    #: 本行对构件评分的扣分（设计 §13）。
    #:
    #: H21 按「同一构件、同一指标只按最重标度扣一次」计分，所以扣分是 (构件, 指标)
    #: 这一组的属性。同组内只有被计入的那条带扣分值，其余是 0——0 表示"按规范没有
    #: 额外扣分"，None 表示"没有正式评定，无从谈起"，两者不能混。
    deduction: float | None = None
    component_score: float | None = None
    photo_numbers: list[str] = Field(default_factory=list)


class PartComparison(ContextModel):
    current_source_defect_count: int
    previous_source_defect_count: int
    delta: int
    has_previous: bool


class ReportStructurePart(ContextModel):
    part_code: str
    part_label: str
    defect_rows: list[ReportDefectRow] = Field(default_factory=list)
    photos: list[ReportPhoto] = Field(default_factory=list)
    comparison: PartComparison

    @model_validator(mode="after")
    def check_photo_numbering_is_consistent(self) -> "ReportStructurePart":
        """病害表的「照片编号」列与图题必须是同一批号码，顺序也一致。

        这是本方案唯一的致命失败模式（设计 §11.8）：两处各算各的，读者按表里的号
        就找不到图。C++ 侧用同一个变量同时写两处，这里再验一次——正因为它在那边
        看起来"不可能出错"，出错时才没人会去查。
        """
        from_table = [number for row in self.defect_rows for number in row.photo_numbers]
        from_captions = [photo.report_number for photo in self.photos]
        if from_table != from_captions:
            raise ValueError(
                f"{self.part_code} 的照片编号不一致：病害表列 {from_table}，"
                f"图题 {from_captions}"
            )
        return self


class ReportAssessmentPart(ContextModel):
    part_code: str
    part_label: str
    score: float
    grade: str | None = None
    weight: float | None = None


class ReportScoreBand(ContextModel):
    """某个部件类别下、同一构件评分的一档（表4.1-2 的一个子行）。"""

    score: float
    component_count: int


class ReportAssessmentCategory(ContextModel):
    part_code: str
    part_label: str
    category_id: str
    category_name: str | None = None
    component_count: int
    score: float
    grade: str | None = None
    weight: float | None = None
    #: 构件评分分档，按分数从低到高。
    score_bands: list[ReportScoreBand] = Field(default_factory=list)

    @model_validator(mode="after")
    def check_bands_cover_every_component(self) -> "ReportAssessmentCategory":
        """分档里的构件数必须正好等于该部件的构件总数。

        表4.1-2 的读者会把这一列加起来核对构件数量；对不上说明有构件被漏掉或
        重复计入，那是比数字难看得多的问题。
        """
        if self.score_bands:
            total = sum(band.component_count for band in self.score_bands)
            if total != self.component_count:
                raise ValueError(
                    f"{self.category_id} 的构件评分分档合计 {total} 与构件数量 "
                    f"{self.component_count} 不符"
                )
        return self


class ReportTopDeduction(ContextModel):
    part_code: str
    component_number: str | None = None
    defect_type: str
    deduction: float


class ReportComponentWeight(ContextModel):
    """部件权重计算表（表4.1-1）的一行。"""

    part_code: str
    part_label: str
    order: int
    category_id: str
    category_name: str | None = None
    configured_weight: float
    #: 重分配后的权重；本桥没有这个部件时为空。
    effective_weight: float | None = None
    component_count: int | None = None
    #: 本桥是否有这个部件。没有时表里注明"无此构件"，权重摊给同部位其余部件。
    present: bool

    @model_validator(mode="after")
    def check_absent_rows_have_no_weight(self) -> "ReportComponentWeight":
        """没有的部件不能带重分配后权重——那正是它被摊掉的意思。"""
        if not self.present and self.effective_weight is not None:
            raise ValueError(f"{self.category_id} 标为无此构件，却带着重新分配后权重")
        return self


class ReportControlIndicator(ContextModel):
    """触发的单项控制指标（H21 4.3）。"""

    rule_id: str
    message: str
    grade_after: str | None = None


class ReportAssessment(ContextModel):
    """当前正式评定的结果（设计 §13）。绝不重跑评定，也绝不读旧 Word 里的评分。"""

    has_formal_run: bool
    overall_score: float | None = None
    overall_grade: str | None = None
    parts: list[ReportAssessmentPart] = Field(default_factory=list)
    categories: list[ReportAssessmentCategory] = Field(default_factory=list)
    #: 主要扣分病害，扣分从大到小，供第 5 章结论概括（设计 §14 第 3 条）。
    top_deductions: list[ReportTopDeduction] = Field(default_factory=list)
    #: 部件权重计算表（表4.1-1）。含本桥没有的部件。
    component_weights: list[ReportComponentWeight] = Field(default_factory=list)
    #: 触发的单项控制指标；为空即"不符合任何一条"。
    triggered_controls: list[ReportControlIndicator] = Field(default_factory=list)

    @model_validator(mode="after")
    def check_results_need_a_run(self) -> "ReportAssessment":
        """没有正式评定就不该带着评分。

        评分出现在没有评定的上下文里，只可能来自臆造或陈旧数据——第 4 章和结论
        都靠它，宁可在这里炸掉。
        """
        if not self.has_formal_run and (
            self.overall_score is not None or self.parts or self.categories
        ):
            raise ValueError("没有当前正式评定，却带着评定结果")
        return self


class ReportBridgeProfile(ContextModel):
    """桥梁概况：只放档案里真有的事实，缺的字段不出这一行。"""

    business_code: str | None = None
    station_mark: str | None = None
    bridge_type: str | None = None
    bridge_scale: str | None = None
    span_combination: str | None = None
    bridge_length_m: float | None = None
    bridge_width_m: float | None = None
    built_year: int | None = None
    maintenance_org: str | None = None

    # §1.1「桥梁概况」的叙述文字要用到的事实。正式报告里那三段话的每一个数和每一个
    # 词都出自下面这些项；缺哪一项就少写哪一句（措辞规则在 conclusion.py）。
    skew_angle_deg: float | None = None
    #: §1.1 印作「桥面净宽」，附录2 第 24 格印作「行车道宽」，同一个量。
    carriageway_width_m: float | None = None
    #: 单侧人行道宽度。
    sidewalk_width_m: float | None = None
    deck_pavement: str | None = None
    expansion_joint_type: str | None = None
    #: 设伸缩缝的墩号，原样印出用户录入的写法。
    expansion_joint_piers: str | None = None
    bearing_type: str | None = None
    superstructure_form: str | None = None
    girders_per_span: int | None = None
    girder_height_m: float | None = None
    abutment_form: str | None = None
    pier_form: str | None = None
    foundation_form: str | None = None
    design_load: str | None = None
    design_org: str | None = None
    construction_org: str | None = None
    supervision_org: str | None = None


class ReportPersonnelEntry(ContextModel):
    full_name: str
    organization: str | None = None
    professional_title: str | None = None
    qualification_certificate_no: str | None = None
    role_code: str


class ReportEquipmentEntry(ContextModel):
    equipment_name: str
    model_spec: str | None = None
    asset_number: str | None = None
    measurement_range: str | None = None
    accuracy: str | None = None
    calibration_certificate_no: str | None = None
    calibration_valid_until: str | None = None
    purpose: str | None = None


class ReportBridgeMedia(ContextModel):
    """一张桥梁图件：报告 §1.1 的地理位置图、示意图或桥梁照片。

    图号不在这里定。病害照片的图号由 C++ 现编，是因为病害表要引用它；§1.1 的图
    只在本节内被引用，按当前有的图在渲染时连续编号更简单，缺一张也不会跳号。
    """

    #: 槽位代码，决定这张图归哪一组、排第几、题注怎么写。
    slot: str
    #: 相对归档根目录的路径，由调用方拼成绝对路径。
    storage_relative_path: str


class ReportContext(ContextModel):
    inspection_year_id: str
    template_id: str
    template_code: str
    #: 即 report_templates.contract_config_json：编号格式和所需人员角色。
    template_config: dict = Field(default_factory=dict)

    #: 标量占位符的取值，键即 {{name}} 里的 name（设计 §7.2 的闭集）。全部已转成
    #: 字符串或 null——替换只往文档里写文本，不该在这一层再做一遍格式化。
    scalars: dict[str, str | None] = Field(default_factory=dict)

    parts: list[ReportStructurePart] = Field(default_factory=list)
    personnel: list[ReportPersonnelEntry] = Field(default_factory=list)
    equipment: list[ReportEquipmentEntry] = Field(default_factory=list)
    assessment: ReportAssessment
    bridge_profile: ReportBridgeProfile = Field(default_factory=ReportBridgeProfile)
    #: §1.1 的图件。没有就不出图，也不写「见图 1-1」这类引用句。
    bridge_media: list[ReportBridgeMedia] = Field(default_factory=list)
    overall_comparison: PartComparison

    def part(self, part_code: str) -> ReportStructurePart | None:
        for item in self.parts:
            if item.part_code == part_code:
                return item
        return None

    def number_format(self, key: str) -> str | None:
        """模板给某个内容块配的编号格式，如 "表2.1-{n}"（设计 §7.5）。"""
        formats = self.template_config.get("table_number_formats") or {}
        value = formats.get(key)
        return value if isinstance(value, str) else None
