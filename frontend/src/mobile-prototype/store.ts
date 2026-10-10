import { blankTask, type TaskState } from "./model";

const DB_NAME = "bridge-field-prototype-v1";
const STORE_NAME = "task";
const TASK_KEY = "donghe-2026";

function openDatabase(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains(STORE_NAME)) {
        request.result.createObjectStore(STORE_NAME);
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error("无法打开本地数据库"));
  });
}

export async function loadTask(): Promise<TaskState> {
  const db = await openDatabase();
  try {
    return await new Promise<TaskState>((resolve, reject) => {
      const transaction = db.transaction(STORE_NAME, "readonly");
      const request = transaction.objectStore(STORE_NAME).get(TASK_KEY);
      request.onsuccess = () => {
        const saved = request.result as Partial<TaskState> | undefined;
        resolve(saved ? {
          defects: saved.defects ?? [],
          reviews: saved.reviews ?? {},
          newChecked: saved.newChecked ?? {},
          completed: saved.completed ?? {},
          submissions: saved.submissions ?? {},
          syncedByInspector: saved.syncedByInspector ?? {},
          lastSyncedAt: saved.lastSyncedAt ?? {},
          dirty: saved.dirty ?? {},
        } : blankTask());
      };
      request.onerror = () => reject(request.error ?? new Error("读取本地记录失败"));
    });
  } finally {
    db.close();
  }
}

export async function saveTask(task: TaskState): Promise<void> {
  const db = await openDatabase();
  try {
    await new Promise<void>((resolve, reject) => {
      const transaction = db.transaction(STORE_NAME, "readwrite");
      transaction.objectStore(STORE_NAME).put(task, TASK_KEY);
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error ?? new Error("保存本地记录失败"));
      transaction.onabort = () => reject(transaction.error ?? new Error("保存本地记录中断"));
    });
  } finally {
    db.close();
  }
}

export async function clearTask(): Promise<void> {
  const db = await openDatabase();
  try {
    await new Promise<void>((resolve, reject) => {
      const transaction = db.transaction(STORE_NAME, "readwrite");
      transaction.objectStore(STORE_NAME).delete(TASK_KEY);
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error ?? new Error("清除本地记录失败"));
    });
  } finally {
    db.close();
  }
}

export function imageToDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(reader.error ?? new Error("照片读取失败"));
    reader.onload = () => {
      const image = new Image();
      image.onerror = () => reject(new Error("照片格式无法读取"));
      image.onload = () => {
        const factor = Math.min(1, 1280 / Math.max(image.width, image.height));
        const canvas = document.createElement("canvas");
        canvas.width = Math.max(1, Math.round(image.width * factor));
        canvas.height = Math.max(1, Math.round(image.height * factor));
        const context = canvas.getContext("2d");
        if (!context) {
          reject(new Error("无法处理照片"));
          return;
        }
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
        resolve(canvas.toDataURL("image/jpeg", 0.8));
      };
      image.src = String(reader.result);
    };
    reader.readAsDataURL(file);
  });
}


