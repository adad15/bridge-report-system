import { useCallback, useEffect, useState, type FormEvent } from "react";

import {
  bridgeProfileError,
  fetchBridgeProfile,
  saveBridgeProfile,
  type BridgeProfile,
  type BridgeProfileField,
  type BridgeProfileInput,
} from "../api/bridgeProfileApi";
import { backendBaseUrl } from "../config";

interface ProfileFieldSpec {
  key: BridgeProfileField;
  label: string;
  /** number 的输入框限数字；其余按原样保存，不猜格式。 */
  kind?: "number";
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

function fieldText(profile: BridgeProfile, field: ProfileFieldSpec): string {
  const value = profile[field.key];
  if (value === null || value === undefined) return "";
  return String(value);
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
 */
export function BridgeProfileCard({ bridgeId, canEdit }: { bridgeId: string; canEdit: boolean }) {
  const [profile, setProfile] = useState<BridgeProfile | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [editing, setEditing] = useState(false);

  const load = useCallback(() => {
    let cancelled = false;
    fetchBridgeProfile(backendBaseUrl, bridgeId)
      .then((body) => {
        if (!cancelled) {
          setProfile(body);
          setError(null);
        }
      })
      .catch((caught: unknown) => {
        if (!cancelled) setError(bridgeProfileError(caught));
      });
    return () => {
      cancelled = true;
    };
  }, [bridgeId]);

  useEffect(() => load(), [load]);

  const total = ALL_FIELDS.length;
  const filled = profile ? filledCount(profile) : 0;

  return (
    <section className="workspace-card bridge-profile-card">
      <header className="bridge-profile-head">
        <h2>桥梁概况</h2>
        <div className="bridge-profile-head-right">
          {profile ? <span className="bridge-profile-progress">已录 {filled} / {total} 项</span> : null}
          {canEdit && profile ? (
            <button type="button" className="bridge-profile-edit" onClick={() => setEditing(true)}>
              {filled === 0 ? "录入" : "编辑"}
            </button>
          ) : null}
        </div>
      </header>

      {error ? <p className="error-text" role="alert">{error}</p> : null}
      {!profile && !error ? <p className="bridge-profile-note">正在加载桥梁档案…</p> : null}

      {profile && filled === 0 ? (
        <p className="bridge-profile-note">
          档案还没录。报告第 1.1 节「桥梁概况」和附录2 卡片都从这里取数，没录就是空的。
        </p>
      ) : null}

      {profile && filled > 0 ? (
        <div className="bridge-profile-body">
          {BRIDGE_PROFILE_SECTIONS.map((section) => {
            const present = section.fields.filter((field) => fieldText(profile, field) !== "");
            if (present.length === 0) return null;
            return (
              <div key={section.title} className="bridge-profile-section">
                <h3>{section.title}</h3>
                <dl>
                  {present.map((field) => (
                    <div key={field.key}>
                      <dt>{field.label}</dt>
                      <dd>{fieldText(profile, field)}{field.unit ?? ""}</dd>
                    </div>
                  ))}
                </dl>
              </div>
            );
          })}
        </div>
      ) : null}

      {editing && profile ? (
        <BridgeProfileDialog
          profile={profile}
          onClose={() => setEditing(false)}
          onSaved={(saved) => {
            setProfile(saved);
            setEditing(false);
          }}
        />
      ) : null}
    </section>
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
  // 表单一开始就装满当前值：保存是整体覆盖，漏装的项会被这次提交抹掉。
  const [values, setValues] = useState<Record<string, string>>(() =>
    Object.fromEntries(ALL_FIELDS.map((field) => [field.key, fieldText(profile, field)]))
  );
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function submit(event: FormEvent) {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const input: BridgeProfileInput = {};
      for (const field of ALL_FIELDS) input[field.key] = values[field.key]?.trim() ?? "";
      onSaved(await saveBridgeProfile(backendBaseUrl, profile.bridge_id, input));
    } catch (caught) {
      setError(bridgeProfileError(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form
        className="workspace-dialog bridge-profile-dialog"
        role="dialog"
        aria-modal="true"
        aria-labelledby="bridge-profile-title"
        onSubmit={(event) => void submit(event)}
      >
        <div className="wizard-head">
          <h2 id="bridge-profile-title">桥梁概况 · {profile.bridge_name}</h2>
          <p className="wizard-standard-note">
            清空某一项就是把它从档案里去掉，报告里对应的那半句话也随之不出。
          </p>
        </div>

        <div className="wizard-body">
          {BRIDGE_PROFILE_SECTIONS.map((section) => (
            <fieldset key={section.title} className="bridge-profile-fieldset">
              <legend>{section.title}</legend>
              <div className="bridge-form-grid">
                {section.fields.map((field) => (
                  <label key={field.key}>
                    <span className="field-label">
                      {field.label}{field.unit ? `（${field.unit}）` : ""}
                    </span>
                    <input
                      type={field.kind === "number" ? "number" : "text"}
                      step={field.kind === "number" ? "any" : undefined}
                      maxLength={field.kind === "number" ? undefined : 200}
                      placeholder={field.placeholder}
                      value={values[field.key] ?? ""}
                      onChange={(event) =>
                        setValues((current) => ({ ...current, [field.key]: event.target.value }))
                      }
                    />
                  </label>
                ))}
              </div>
            </fieldset>
          ))}
        </div>

        <div className="wizard-foot">
          {error ? <p className="error-text" role="alert">{error}</p> : null}
          <div className="dialog-actions">
            <button type="button" disabled={busy} onClick={onClose}>取消</button>
            <button type="submit" className="primary-button" disabled={busy}>
              {busy ? "正在保存…" : "保存档案"}
            </button>
          </div>
        </div>
      </form>
    </div>
  );
}
