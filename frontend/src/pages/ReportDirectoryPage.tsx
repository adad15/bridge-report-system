import { PlusOutlined, ReloadOutlined } from "@ant-design/icons";
import {
  Alert,
  Button,
  Card,
  Col,
  Empty,
  Flex,
  Form,
  Input,
  Modal,
  Popconfirm,
  Row,
  Space,
  Table,
  Tabs,
  Tag,
  Tooltip,
  Typography,
  type TableProps,
} from "antd";
import { useCallback, useEffect, useState } from "react";

import {
  createReportEquipment,
  createReportPersonnel,
  deleteReportEquipment,
  deleteReportPersonnel,
  fetchReportEquipment,
  fetchReportPersonnel,
  setReportEquipmentEnabled,
  setReportPersonnelEnabled,
  updateReportEquipment,
  updateReportPersonnel,
  type ReportEquipment,
  type ReportEquipmentInput,
  type ReportPersonnel,
  type ReportPersonnelInput,
} from "../api/reportApi";
import { useAuth } from "../auth/AuthContext";
import { PageHeader } from "../design-system";
import { reportErrorMessage } from "../report/reportErrors";

/**
 * 系统管理 · 报告人员与检测设备（设计 §21.2、§21.3）。
 *
 * 两个库共用一条规则：**被年度配置引用过的条目不许硬删，只能停用。** 删掉会让历史
 * 报告配置指向一个不存在的人或设备，而报告是对外交付物。界面按 assignment_count
 * 直接把删除按钮禁掉，不让用户点下去撞外键。
 */
export function ReportDirectoryPage() {
  const { user } = useAuth();
  const isAdmin = user?.role === "admin";
  const [tab, setTab] = useState("personnel");

  return (
    <Flex vertical gap={16}>
      <PageHeader
        title="报告人员与设备"
        description="签字人员与检测设备的公共库。年度报告配置从这里挑，不在每个年度各录一份。"
      />

      <Tabs
        activeKey={tab}
        onChange={setTab}
        items={[
          { key: "personnel", label: "报告人员", children: <PersonnelTab isAdmin={isAdmin} /> },
          { key: "equipment", label: "检测设备", children: <EquipmentTab isAdmin={isAdmin} /> },
        ]}
      />
    </Flex>
  );
}

/** 停用项仍然列出来，只是标出来——历史配置要看得见（设计 §15.3）。 */
function EnabledTag({ enabled }: { enabled: boolean }) {
  return <Tag color={enabled ? "blue" : "default"}>{enabled ? "启用" : "停用"}</Tag>;
}

function PersonnelTab({ isAdmin }: { isAdmin: boolean }) {
  const [items, setItems] = useState<ReportPersonnel[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [editing, setEditing] = useState<ReportPersonnel | "new" | null>(null);

  const load = useCallback(async () => {
    setError(null);
    try {
      setItems(await fetchReportPersonnel());
    } catch (caught) {
      setError(reportErrorMessage(caught));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  async function run(action: () => Promise<unknown>) {
    setBusy(true);
    setError(null);
    try {
      await action();
      await load();
    } catch (caught) {
      setError(reportErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  const columns: TableProps<ReportPersonnel>["columns"] = [
    { title: "姓名", dataIndex: "full_name", key: "full_name", width: 120 },
    { title: "单位", dataIndex: "organization", key: "organization", width: 200, ellipsis: true,
      render: (value: string | null) => value ?? "—" },
    { title: "职称", dataIndex: "professional_title", key: "professional_title", width: 120,
      render: (value: string | null) => value ?? "—" },
    { title: "资格证书编号", dataIndex: "qualification_certificate_no", key: "certificate",
      width: 180, ellipsis: true, render: (value: string | null) => value ?? "—" },
    { title: "联系方式", key: "contact", width: 200, ellipsis: true,
      render: (_value, item) => [item.phone, item.email].filter(Boolean).join(" · ") || "—" },
    { title: "状态", key: "state", width: 140,
      render: (_value, item) => (
        <Space size={4}>
          <EnabledTag enabled={item.is_enabled} />
          {item.assignment_count > 0 ? (
            <Tooltip title="被年度报告配置引用，只能停用不能删除">
              <Tag color="cyan">被引用 {item.assignment_count}</Tag>
            </Tooltip>
          ) : null}
        </Space>
      ) },
    ...(isAdmin
      ? [{
          title: "操作",
          key: "actions",
          width: 180,
          fixed: "right" as const,
          render: (_value: unknown, item: ReportPersonnel) => (
            <Space size={2} wrap={false}>
              <Button type="link" size="small" onClick={() => setEditing(item)}>编辑</Button>
              <Button type="link" size="small" disabled={busy}
                onClick={() => void run(() => setReportPersonnelEnabled(item.id, !item.is_enabled))}>
                {item.is_enabled ? "停用" : "启用"}
              </Button>
              <Popconfirm title="删除这位人员？" okText="删除" cancelText="取消"
                disabled={item.assignment_count > 0}
                onConfirm={() => void run(() => deleteReportPersonnel(item.id))}>
                <Button type="link" size="small" danger disabled={item.assignment_count > 0}>
                  删除
                </Button>
              </Popconfirm>
            </Space>
          ),
        }]
      : []),
  ];

  return (
    <>
      {error ? <Alert type="error" showIcon title={error} closable onClose={() => setError(null)} /> : null}
      <Card
        title={`人员（${items?.length ?? 0}）`}
        extra={
          <Space>
            <Button icon={<ReloadOutlined />} onClick={() => void load()}>刷新</Button>
            {isAdmin ? (
              <Button type="primary" icon={<PlusOutlined />} onClick={() => setEditing("new")}>
                新增人员
              </Button>
            ) : null}
          </Space>
        }
      >
        <Table<ReportPersonnel>
          rowKey="id"
          loading={items === null && error === null}
          columns={columns}
          dataSource={items ?? []}
          pagination={false}
          scroll={{ x: 1100 }}
          locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="人员库还是空的" /> }}
        />
      </Card>
      <PersonnelModal
        target={editing}
        onClose={() => setEditing(null)}
        onSaved={() => { setEditing(null); void load(); }}
      />
    </>
  );
}

function PersonnelModal({
  target,
  onClose,
  onSaved,
}: {
  target: ReportPersonnel | "new" | null;
  onClose: () => void;
  onSaved: () => void;
}) {
  const existing = target === "new" || target === null ? null : target;
  const [form, setForm] = useState<ReportPersonnelInput>({ full_name: "" });
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (target === null) return;
    setError(null);
    setForm(
      existing
        ? {
            full_name: existing.full_name,
            organization: existing.organization ?? undefined,
            job_title: existing.job_title ?? undefined,
            professional_title: existing.professional_title ?? undefined,
            qualification_certificate_no: existing.qualification_certificate_no ?? undefined,
            phone: existing.phone ?? undefined,
            email: existing.email ?? undefined,
            remarks: existing.remarks ?? undefined,
          }
        : { full_name: "" },
    );
  }, [target, existing]);

  const field = (key: keyof ReportPersonnelInput, label: string, placeholder?: string) => (
    <Col xs={24} sm={12} key={key}>
      <Form.Item label={label} htmlFor={`report-directory-${key}`}>
        <Input
          id={`report-directory-${key}`}
          value={form[key] ?? ""}
          placeholder={placeholder}
          onChange={(event) => setForm({ ...form, [key]: event.target.value })}
        />
      </Form.Item>
    </Col>
  );

  async function submit() {
    if (!form.full_name.trim()) {
      setError("姓名不能为空。");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (existing) await updateReportPersonnel(existing.id, form);
      else await createReportPersonnel(form);
      onSaved();
    } catch (caught) {
      setError(reportErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open={target !== null}
      centered
      width={640}
      title={existing ? `编辑「${existing.full_name}」` : "新增报告人员"}
      okText="保存"
      cancelText="取消"
      confirmLoading={busy}
      onOk={() => void submit()}
      onCancel={onClose}
    >
      <Form layout="vertical">
        <Row gutter={16}>
        {field("full_name", "姓名")}
        {field("organization", "单位")}
        {field("job_title", "岗位")}
        {field("professional_title", "职称", "教授级高工")}
        {field("qualification_certificate_no", "资格证书编号")}
        {field("phone", "电话")}
        {field("email", "邮箱")}
        {field("remarks", "备注")}
        </Row>
      </Form>
      {error ? <Alert type="error" showIcon title={error} /> : null}
    </Modal>
  );
}

function EquipmentTab({ isAdmin }: { isAdmin: boolean }) {
  const [items, setItems] = useState<ReportEquipment[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [editing, setEditing] = useState<ReportEquipment | "new" | null>(null);

  const load = useCallback(async () => {
    setError(null);
    try {
      setItems(await fetchReportEquipment());
    } catch (caught) {
      setError(reportErrorMessage(caught));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  async function run(action: () => Promise<unknown>) {
    setBusy(true);
    setError(null);
    try {
      await action();
      await load();
    } catch (caught) {
      setError(reportErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  const columns: TableProps<ReportEquipment>["columns"] = [
    { title: "设备名称", dataIndex: "equipment_name", key: "equipment_name", width: 160, ellipsis: true },
    { title: "型号规格", dataIndex: "model_spec", key: "model_spec", width: 140,
      render: (value: string | null) => value ?? "—" },
    { title: "资产编号", dataIndex: "asset_number", key: "asset_number", width: 130,
      render: (value: string | null) => value ?? "—" },
    { title: "量程 / 精度", key: "range", width: 160, ellipsis: true,
      render: (_value, item) => [item.measurement_range, item.accuracy].filter(Boolean).join(" / ") || "—" },
    { title: "检定有效期", key: "calibration", width: 170,
      render: (_value, item) => <CalibrationCell item={item} /> },
    { title: "状态", key: "state", width: 140,
      render: (_value, item) => (
        <Space size={4}>
          <EnabledTag enabled={item.is_enabled} />
          {item.assignment_count > 0 ? (
            <Tooltip title="被年度报告配置引用，只能停用不能删除">
              <Tag color="cyan">被引用 {item.assignment_count}</Tag>
            </Tooltip>
          ) : null}
        </Space>
      ) },
    ...(isAdmin
      ? [{
          title: "操作",
          key: "actions",
          width: 180,
          fixed: "right" as const,
          render: (_value: unknown, item: ReportEquipment) => (
            <Space size={2} wrap={false}>
              <Button type="link" size="small" onClick={() => setEditing(item)}>编辑</Button>
              <Button type="link" size="small" disabled={busy}
                onClick={() => void run(() => setReportEquipmentEnabled(item.id, !item.is_enabled))}>
                {item.is_enabled ? "停用" : "启用"}
              </Button>
              <Popconfirm title="删除这台设备？" okText="删除" cancelText="取消"
                disabled={item.assignment_count > 0}
                onConfirm={() => void run(() => deleteReportEquipment(item.id))}>
                <Button type="link" size="small" danger disabled={item.assignment_count > 0}>
                  删除
                </Button>
              </Popconfirm>
            </Space>
          ),
        }]
      : []),
  ];

  return (
    <>
      {error ? <Alert type="error" showIcon title={error} closable onClose={() => setError(null)} /> : null}
      <Card
        title={`设备（${items?.length ?? 0}）`}
        extra={
          <Space>
            <Button icon={<ReloadOutlined />} onClick={() => void load()}>刷新</Button>
            {isAdmin ? (
              <Button type="primary" icon={<PlusOutlined />} onClick={() => setEditing("new")}>
                新增设备
              </Button>
            ) : null}
          </Space>
        }
      >
        <Table<ReportEquipment>
          rowKey="id"
          loading={items === null && error === null}
          columns={columns}
          dataSource={items ?? []}
          pagination={false}
          scroll={{ x: 1120 }}
          locale={{ emptyText: <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="设备库还是空的" /> }}
        />
      </Card>
      <EquipmentModal
        target={editing}
        onClose={() => setEditing(null)}
        onSaved={() => { setEditing(null); void load(); }}
      />
    </>
  );
}

/** 检定有效期：过期要显眼。设备过期不阻断保存，但报告里会如实印出来。 */
function CalibrationCell({ item }: { item: ReportEquipment }) {
  if (!item.calibration_valid_until) return <Typography.Text type="secondary">—</Typography.Text>;
  const expired = new Date(item.calibration_valid_until) < new Date();
  return (
    <Space size={4}>
      <span>{item.calibration_valid_until}</span>
      {expired ? <Tag color="red">已过期</Tag> : null}
    </Space>
  );
}

function EquipmentModal({
  target,
  onClose,
  onSaved,
}: {
  target: ReportEquipment | "new" | null;
  onClose: () => void;
  onSaved: () => void;
}) {
  const existing = target === "new" || target === null ? null : target;
  const [form, setForm] = useState<ReportEquipmentInput>({ equipment_name: "" });
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (target === null) return;
    setError(null);
    setForm(
      existing
        ? {
            equipment_name: existing.equipment_name,
            model_spec: existing.model_spec ?? undefined,
            asset_number: existing.asset_number ?? undefined,
            measurement_range: existing.measurement_range ?? undefined,
            accuracy: existing.accuracy ?? undefined,
            calibration_certificate_no: existing.calibration_certificate_no ?? undefined,
            calibration_valid_until: existing.calibration_valid_until ?? undefined,
            remarks: existing.remarks ?? undefined,
          }
        : { equipment_name: "" },
    );
  }, [target, existing]);

  const field = (key: keyof ReportEquipmentInput, label: string, placeholder?: string) => (
    <Col xs={24} sm={12} key={key}>
      <Form.Item label={label} htmlFor={`report-directory-${key}`}>
        <Input
          id={`report-directory-${key}`}
          value={form[key] ?? ""}
          placeholder={placeholder}
          onChange={(event) => setForm({ ...form, [key]: event.target.value })}
        />
      </Form.Item>
    </Col>
  );

  async function submit() {
    if (!form.equipment_name.trim()) {
      setError("设备名称不能为空。");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (existing) await updateReportEquipment(existing.id, form);
      else await createReportEquipment(form);
      onSaved();
    } catch (caught) {
      setError(reportErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <Modal
      open={target !== null}
      centered
      width={640}
      title={existing ? `编辑「${existing.equipment_name}」` : "新增检测设备"}
      okText="保存"
      cancelText="取消"
      confirmLoading={busy}
      onOk={() => void submit()}
      onCancel={onClose}
    >
      <Form layout="vertical">
        <Row gutter={16}>
        {field("equipment_name", "设备名称", "裂缝观测仪")}
        {field("model_spec", "型号规格", "ZBL-F130")}
        {field("asset_number", "资产编号")}
        {field("measurement_range", "量程", "0-6mm")}
        {field("accuracy", "精度", "0.01mm")}
        {field("calibration_certificate_no", "检定证书编号")}
        {field("calibration_valid_until", "检定有效期", "2027-01-31")}
        {field("remarks", "备注")}
        </Row>
      </Form>
      {error ? <Alert type="error" showIcon title={error} /> : null}
    </Modal>
  );
}
