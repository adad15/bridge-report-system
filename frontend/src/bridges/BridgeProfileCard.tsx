import {
  Alert,
  Button,
  Card,
  Col,
  Descriptions,
  Divider,
  Flex,
  Form,
  Input,
  Modal,
  Row,
  Typography,
  type FormRule,
} from "antd";
import { Fragment, useState } from "react";

import { BRIDGE_MEDIA_SLOTS, type BridgeMedia, type BridgeMediaSlot } from "../api/bridgeMediaApi";
import {
  bridgeProfileError,
  saveBridgeProfile,
  type BridgeProfile,
  type BridgeProfileField,
  type BridgeProfileInput,
} from "../api/bridgeProfileApi";
import { backendBaseUrl } from "../config";
import { BridgeMediaDialog } from "./BridgeMediaDialog";
import {
  formatCoordinates,
  formatDms,
  parseCoordinate,
  type Axis,
} from "./coordinates";

interface ProfileFieldSpec {
  key: BridgeProfileField;
  label: string;
  /**
   * number 的输入框限数字；coordinate 收度分秒也收十进制度，存进库里的总是
   * 十进制度；其余按原样保存，不猜格式。
   */
  kind?: "number" | "coordinate";
  /** coordinate 字段属于哪一轴，决定半球字母是 NS 还是 EW。 */
  axis?: Axis;
  /** 单位只用于展示，不进数据库。 */
  unit?: string;
  placeholder?: string;
}

interface ProfileSection {
  title: string;
  fields: ProfileFieldSpec[];
}

/**
 * 档案分组：和报告 §1.1 那几句话的次序一致——先位置，再规模与宽度，再桥面，
 * 再上下部结构，最后参建单位。录入时按这个次序找字段，对着正式报告抄最省事。
 */
export const BRIDGE_PROFILE_SECTIONS: ProfileSection[] = [
  {
    title: "位置",
    fields: [
      { key: "business_code", label: "桥梁编码" },
      { key: "route_number", label: "路线编号", placeholder: "S320" },
      { key: "route_name", label: "路线名称", placeholder: "大养线" },
      { key: "administrative_region", label: "行政区划", placeholder: "锦州段" },
      { key: "station_mark", label: "中心桩号", placeholder: "K109+747" },
      { key: "longitude", label: "经度", kind: "coordinate", axis: "lng", placeholder: "E121°11'46.7\"" },
      { key: "latitude", label: "纬度", kind: "coordinate", axis: "lat", placeholder: "N41°6'55.2\"" },
    ],
  },
  {
    title: "规模",
    fields: [
      { key: "bridge_type", label: "桥梁类型" },
      { key: "bridge_scale", label: "桥梁规模", placeholder: "大桥" },
      { key: "span_combination", label: "跨径布置", placeholder: "33×20.0m" },
      { key: "bridge_length_m", label: "桥梁全长", kind: "number", unit: "m" },
      { key: "bridge_width_m", label: "桥面总宽", kind: "number", unit: "m" },
      { key: "carriageway_width_m", label: "桥面净宽", kind: "number", unit: "m" },
      { key: "sidewalk_width_m", label: "人行道宽度", kind: "number", unit: "m", placeholder: "单侧" },
      { key: "skew_angle_deg", label: "斜交角", kind: "number", unit: "°" },
      { key: "built_year", label: "建成年份", kind: "number", unit: "年" },
    ],
  },
  {
    title: "桥面系",
    fields: [
      { key: "deck_pavement", label: "桥面铺装", placeholder: "沥青混凝土" },
      { key: "expansion_joint_type", label: "伸缩缝形式", placeholder: "型钢伸缩缝" },
      { key: "expansion_joint_piers", label: "设缝墩号", placeholder: "2、6、10" },
      { key: "bearing_type", label: "支座形式", placeholder: "板式橡胶支座" },
    ],
  },
  {
    title: "结构",
    fields: [
      { key: "superstructure_form", label: "上部结构", placeholder: "预应力砼简支空心板" },
      { key: "girders_per_span", label: "每孔片数", kind: "number", unit: "片" },
      { key: "girder_height_m", label: "梁高", kind: "number", unit: "m" },
      { key: "abutment_form", label: "桥台", placeholder: "钢筋砼肋板台、桩基础" },
      { key: "pier_form", label: "桥墩", placeholder: "钢筋砼四柱式墩、桩基础" },
      { key: "foundation_form", label: "基础" },
      { key: "design_load", label: "设计荷载", placeholder: "公路-Ⅰ级" },
    ],
  },
  {
    title: "参建与管理单位",
    fields: [
      { key: "design_org", label: "设计单位" },
      { key: "construction_org", label: "施工单位" },
      { key: "maintenance_org", label: "管养单位" },
      { key: "supervision_org", label: "监管单位" },
    ],
  },
];

const ALL_FIELDS = BRIDGE_PROFILE_SECTIONS.flatMap((section) => section.fields);

const AXIS_NAME: Record<Axis, string> = { lng: "经度", lat: "纬度" };
const AXIS_LIMIT: Record<Axis, number> = { lng: 180, lat: 90 };
const AXIS_EXAMPLE: Record<Axis, string> = { lng: "E121°11'46.7\"，或十进制度 121.1963", lat: "N41°6'55.2\"，或十进制度 41.1153" };

/**
 * 经纬度逐栏校验，错误挂在出问题的那一栏下面。
 *
 * 库里这两列是 `numeric(10,7)`，填个四位数过去报的是「数字字段溢出」，
 * 错误里看不出是哪一栏的事。在这里挡，用户立刻就知道该改哪个框。
 *
 * 只填一个也不行：单独一个经度定不了位，地图和报告都用不上。提示挂在空着的那一栏上。
 */
function coordinateRule(axis: Axis): FormRule {
  const other: BridgeProfileField = axis === "lng" ? "latitude" : "longitude";
  return ({ getFieldValue }) => ({
    validator(_, value: string | undefined) {
      const own = value?.trim() ?? "";
      const pair = String(getFieldValue(other) ?? "").trim();
      if (own === "") {
        return pair === ""
          ? Promise.resolve()
          : Promise.reject(new Error("经度和纬度要一起填，只填一个定不了位。"));
      }
      const parsed = parseCoordinate(own, axis);
      if (parsed === null || !Number.isFinite(parsed)) {
        return Promise.reject(new Error(`读不出来。写成 ${AXIS_EXAMPLE[axis]}。`));
      }
      if (Math.abs(parsed) > AXIS_LIMIT[axis]) {
        const limit = AXIS_LIMIT[axis];
        return Promise.reject(new Error(`${AXIS_NAME[axis]}要在 -${limit} 到 ${limit} 之间，单位是度。`));
      }
      return Promise.resolve();
    },
  });
}

function fieldText(profile: BridgeProfile, field: ProfileFieldSpec): string {
  const value = profile[field.key];
  if (value === null || value === undefined) return "";
  return String(value);
}

/** 编辑框里显示的写法：坐标用度分秒，其余原样。 */
function editableText(profile: BridgeProfile, field: ProfileFieldSpec): string {
  const raw = fieldText(profile, field);
  if (raw === "" || field.kind !== "coordinate" || !field.axis) return raw;
  return formatDms(Number(raw), field.axis);
}

/** 桥位坐标合成一行显示；缺一半就不显示，单独一个经度定不了位。 */
function coordinatesText(profile: BridgeProfile): string {
  const { longitude, latitude } = profile;
  if (longitude === null || latitude === null) return "";
  return formatCoordinates(longitude, latitude);
}

function filledCount(profile: BridgeProfile): number {
  return ALL_FIELDS.filter((field) => fieldText(profile, field) !== "").length;
}

/**
 * 桥梁概况。
 *
 * 取代了原来的「最近动态」：那张卡片列的是最近三次年度检测，而年度检测本来就有自己
 * 的入口，卡片只是把同一串链接又放了一遍。这一份档案不一样——报告 §1.1 的那几段叙述
 * 和附录2 卡片的十几格都从这里取数，档案不录，报告那一节就是空的。
 *
 * 只显示**已经录了**的项。没录的不占位、不写「未知」，跟报告的规矩一致（设计 §14 第 5 条）。
 *
 * 档案由页面取并传进来：地理位置卡片读的是同一份数据，各取各的会发两次请求，
 * 改完坐标后还会一张卡片新、一张卡片旧。
 */
export function BridgeProfileCard({
  profile,
  error,
  canEdit,
  onSaved,
  media = [],
  onMediaReplaced = () => undefined,
  onMediaRemoved = () => undefined,
}: {
  profile: BridgeProfile | null;
  error: string | null;
  canEdit: boolean;
  onSaved: (saved: BridgeProfile) => void;
  media?: BridgeMedia[];
  onMediaReplaced?: (item: BridgeMedia) => void;
  onMediaRemoved?: (slot: BridgeMediaSlot) => void;
}) {
  const [editing, setEditing] = useState(false);
  const [viewingMedia, setViewingMedia] = useState(false);

  const total = ALL_FIELDS.length;
  const filled = profile ? filledCount(profile) : 0;

  return (
    <Card
      size="small"
      title="桥梁概况"
      // 卡片高度由放它的地方给定；录满时有二十几项，超出的部分在卡片内滚动。
      style={{ height: "100%" }}
      styles={{
        root: { display: "flex", flexDirection: "column" },
        body: { flex: 1, minHeight: 0, overflowY: "auto" },
      }}
      extra={profile ? (
        <Flex align="center" gap={10}>
          <Typography.Text type="secondary">已录 {filled} / {total} 项</Typography.Text>
          <Button size="small" onClick={() => setViewingMedia(true)}>
            图件 {media.length} / {BRIDGE_MEDIA_SLOTS.length}
          </Button>
          {canEdit ? (
            <Button size="small" onClick={() => setEditing(true)}>
              {filled === 0 ? "录入" : "编辑"}
            </Button>
          ) : null}
        </Flex>
      ) : null}
    >
      {error ? <Alert type="error" showIcon title={error} /> : null}
      {!profile && !error ? <Typography.Text type="secondary">正在加载桥梁档案…</Typography.Text> : null}

      {profile && filled === 0 ? (
        <Typography.Text type="secondary">
          档案还没录。报告第 1.1 节「桥梁概况」和附录2 卡片都从这里取数，没录就是空的。
        </Typography.Text>
      ) : null}

      {profile && filled > 0 ? (
        <Flex vertical gap={12}>
          {BRIDGE_PROFILE_SECTIONS.map((section) => {
            const rows = section.fields
              .filter((field) => field.kind !== "coordinate" && fieldText(profile, field) !== "")
              .map((field) => ({
                key: String(field.key),
                label: field.label,
                children: fieldText(profile, field) + (field.unit ?? ""),
                span: 1,
              }));
            // 经度纬度合成一行，和工程资料上的写法一致。
            const coordinates = coordinatesText(profile);
            if (section.fields.some((field) => field.kind === "coordinate") && coordinates) {
              // 两个度分秒写满一行，在半列里会折成两截，给它整行。
              rows.push({ key: "coordinates", label: "经纬度坐标", children: coordinates, span: 2 });
            }
            if (rows.length === 0) return null;
            return (
              <Descriptions
                key={section.title}
                size="small"
                column={2}
                title={<Typography.Text type="secondary" strong>{section.title}</Typography.Text>}
                items={rows}
                styles={{ label: { width: 96, whiteSpace: "nowrap" }, header: { marginBottom: 4 } }}
              />
            );
          })}
        </Flex>
      ) : null}

      {viewingMedia && profile ? (
        <BridgeMediaDialog
          bridgeId={profile.bridge_id}
          bridgeName={profile.bridge_name}
          media={media}
          canEdit={canEdit}
          onReplaced={onMediaReplaced}
          onRemoved={onMediaRemoved}
          onClose={() => setViewingMedia(false)}
        />
      ) : null}

      {editing && profile ? (
        <BridgeProfileDialog
          profile={profile}
          onClose={() => setEditing(false)}
          onSaved={(saved) => {
            onSaved(saved);
            setEditing(false);
          }}
        />
      ) : null}
    </Card>
  );
}

function BridgeProfileDialog({
  profile,
  onClose,
  onSaved,
}: {
  profile: BridgeProfile;
  onClose: () => void;
  onSaved: (saved: BridgeProfile) => void;
}) {
  const [form] = Form.useForm<Record<string, string>>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit(values: Record<string, string | undefined>) {
    setBusy(true);
    setError(null);
    try {
      const input: BridgeProfileInput = {};
      for (const field of ALL_FIELDS) {
        // 保存是整体覆盖：每一项都要带上，没碰过的也一样，漏传的项会被这次提交抹掉。
        const text = values[field.key]?.trim() ?? "";
        // 度分秒在这里换成十进制度：库里只存一种写法。
        input[field.key] = field.kind === "coordinate" && field.axis && text !== ""
          ? String(parseCoordinate(text, field.axis))
          : text;
      }
      onSaved(await saveBridgeProfile(backendBaseUrl, profile.bridge_id, input));
    } catch (caught) {
      setError(bridgeProfileError(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open
      centered
      width={940}
      title={`桥梁概况 · ${profile.bridge_name}`}
      okText="保存档案"
      cancelText="取消"
      confirmLoading={busy}
      cancelButtonProps={{ disabled: busy }}
      mask={{ closable: false }}
      onOk={() => form.submit()}
      onCancel={onClose}
      styles={{ body: { maxHeight: "calc(100vh - 220px)", overflowY: "auto", overflowX: "hidden" } }}
    >
      <Typography.Paragraph type="secondary">
        清空某一项就是把它从档案里去掉，报告里对应的那半句话也随之不出。
      </Typography.Paragraph>

      <Form
        form={form}
        layout="vertical"
        requiredMark={false}
        // 表单一开始就装满当前值：保存是整体覆盖。
        initialValues={Object.fromEntries(ALL_FIELDS.map((field) => [field.key, editableText(profile, field)]))}
        onFinish={(values) => void submit(values)}
      >
        {BRIDGE_PROFILE_SECTIONS.map((section) => (
          <Fragment key={section.title}>
            <Divider titlePlacement="start" size="small">{section.title}</Divider>
            <Row gutter={14}>
              {section.fields.map((field) => (
                <Col key={field.key} xs={24} sm={12} md={8}>
                  <Form.Item
                    name={field.key}
                    label={field.label}
                    rules={field.kind === "coordinate" && field.axis ? [coordinateRule(field.axis)] : undefined}
                    dependencies={field.kind === "coordinate"
                      ? [field.key === "longitude" ? "latitude" : "longitude"]
                      : undefined}
                  >
                    <Input
                      type={field.kind === "number" ? "number" : "text"}
                      step={field.kind === "number" ? "any" : undefined}
                      maxLength={field.kind === "number" ? undefined : 200}
                      placeholder={field.placeholder}
                      suffix={field.unit}
                    />
                  </Form.Item>
                </Col>
              ))}
            </Row>
          </Fragment>
        ))}
      </Form>

      {error ? <Alert type="error" showIcon title={error} /> : null}
    </Modal>
  );
}
