import type { AssessmentPreviewResponse } from "../api/assessmentApi";

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
