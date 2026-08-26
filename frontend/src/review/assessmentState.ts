import type { AssessmentPreviewResponse, ConfirmedAssessmentResponse } from "../api/assessmentApi";

export type AssessmentPhase = "idle" | "updating" | "ready" | "blocked" | "error";

export interface AssessmentState {
  phase: AssessmentPhase;
  requestedRevision: number;
  response: AssessmentPreviewResponse | null;
  error: string | null;
}

export const initialAssessmentState: AssessmentState = {
  phase: "idle",
  requestedRevision: -1,
  response: null,
  error: null,
};

export type AssessmentAction =
  | { type: "requested"; revision: number }
  | { type: "resolved"; response: AssessmentPreviewResponse; currentRevision: number }
  | { type: "failed"; revision: number; currentRevision: number; message: string }
  | { type: "reset" };

export function assessmentReducer(state: AssessmentState, action: AssessmentAction): AssessmentState {
  switch (action.type) {
    case "requested":
      return { ...state, phase: "updating", requestedRevision: action.revision, error: null };
    case "resolved":
      if (action.response.client_revision !== action.currentRevision || action.response.client_revision < state.requestedRevision) {
        return state;
      }
      return {
        phase: action.response.result === null ? "blocked" : "ready",
        requestedRevision: action.response.client_revision,
        response: action.response,
        error: null,
      };
    case "failed":
      if (action.revision !== action.currentRevision || action.revision < state.requestedRevision) return state;
      return { ...state, phase: "error", requestedRevision: action.revision, error: action.message };
    case "reset":
      return initialAssessmentState;
  }
}

export function currentAssessmentIssues(
  state: AssessmentState,
  currentRevision: number,
): AssessmentPreviewResponse["issues"] {
  return state.response?.client_revision === currentRevision ? state.response.issues : [];
}

// ── 已入库评定的读取 ──────────────────────────────────────────────────
// 与试算分开成两套状态，是因为两者的失效条件完全不同：试算随草稿每次编辑作废，
// 已入库的那份是既成事实，只在换一条导入记录时才需要重取。把它们塞进同一个 reducer
// 会让 client_revision 这类只对试算成立的判断误伤只读回执。

export type ConfirmedAssessmentPhase = "idle" | "loading" | "ready" | "error";

export interface ConfirmedAssessmentState {
  phase: ConfirmedAssessmentPhase;
  report: ConfirmedAssessmentResponse | null;
  error: string | null;
}

export const initialConfirmedAssessmentState: ConfirmedAssessmentState = {
  phase: "idle",
  report: null,
  error: null,
};

export type ConfirmedAssessmentAction =
  | { type: "loading" }
  | { type: "loaded"; report: ConfirmedAssessmentResponse }
  | { type: "failed"; message: string }
  | { type: "reset" };

export function confirmedAssessmentReducer(
  state: ConfirmedAssessmentState,
  action: ConfirmedAssessmentAction,
): ConfirmedAssessmentState {
  switch (action.type) {
    case "loading":
      // 重取时保留上一份结果：请求在飞的那几百毫秒里，界面不该先把分数清空再填回去。
      return { ...state, phase: "loading", error: null };
    case "loaded":
      return { phase: "ready", report: action.report, error: null };
    case "failed":
      return { phase: "error", report: null, error: action.message };
    case "reset":
      return initialConfirmedAssessmentState;
  }
}
