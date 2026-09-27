export type ResultState = "pending" | "bounded" | "estimated" | "exact";
export interface PathScore { total: number; solo: number; sections: number[]; contributions: number[] }
export interface Extreme { status: ResultState; lower: number; upper: number; witness: PathScore | null; choices: number[] }
export interface Rational { numerator: string; denominator: string }
export interface Expectation {
  status: ResultState; mean: PathScore | null; lower: number; upper: number;
  confidence: number; confidence_samples: number; precision_reached: boolean;
  exact?: { total: Rational; solo: Rational; sections: Rational[]; contributions: Rational[] };
}
export interface Snapshot {
  schema_version: 1; version: string; complete: boolean; stop_reason: string;
  failed: boolean; error: string;
  probability_dag: boolean; merged_states: number;
  source: Record<string, unknown>;
  exact_memory_limited: boolean; analysis_memory_bytes: number;
  pending_nodes: number; evaluations: number; enumerated_leaves: number;
  explored_probability: Rational;
  samples: number; total_power: number;
  results: Array<{ ranks: number[] | null; maximum: Extreme; minimum: Extreme; expectation: Expectation }>;
}
export interface Preparation {
  master: Record<string, unknown>; chart: Uint8Array; chartName: string;
  calculation: { schema_version: 1; music_score_id: number; mode: "free" | "battle" | "arena" | "challenge" | "mission" | "challenge_skip"; gekisou_enabled?: boolean; context?: { missions?: number[]; music_type?: number } };
  deck: Record<string, unknown>;
  options?: { ranks?: number[]; contributions?: boolean; estimator_seed?: number; memory_limit_bytes?: number; exact_memory_limit_bytes?: number };
}
/** One client owns one Worker/session. Inputs stay local to the Worker. */
export class ScoreAnalyzer {
  private worker: Worker;
  private nextId = 1;
  private disposed = false;
  private fault?: Error;
  private pending = new Map<number, { resolve: (value: any) => void; reject: (error: Error) => void }>();
  onProgress?: (snapshot: Snapshot) => void;
  constructor(workerURL: URL | string) {
    this.worker = new Worker(workerURL, { type: "module" });
    this.worker.onmessage = ({ data }) => {
      if (data.type === "progress") { this.onProgress?.(data.snapshot); return; }
      const task = this.pending.get(data.id); if (!task) return;
      this.pending.delete(data.id);
      data.error ? task.reject(new Error(data.error)) : task.resolve(data.result);
    };
    this.worker.onerror = (event) => { this.fault = new Error(event.message || "Worker failed"); for (const task of this.pending.values()) task.reject(this.fault); this.pending.clear(); };
  }
  private call(type: string, payload: object = {}): Promise<any> {
    if (this.disposed) return Promise.reject(new Error("Analyzer disposed"));
    if (this.fault) return Promise.reject(this.fault);
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      try { this.worker.postMessage({ id, type, ...payload }); }
      catch (error) { this.pending.delete(id); reject(error); }
    });
  }
  prepare(input: Preparation): Promise<void> { return this.call("prepare", { input }); }
  run(budgetMs = 30000): Promise<Snapshot> { return this.call("run", { budgetMs }); }
  snapshot(): Promise<Snapshot> { return this.call("snapshot"); }
  cancel(): Promise<Snapshot> { return this.call("cancel"); }
  /** Continue after time/cancel; a memory-frozen exact lane only resumes sampling. */
  resume(budgetMs = 30000): Promise<Snapshot> { return this.call("resume", { budgetMs }); }
  dispose(): void {
    this.disposed = true;
    this.worker.terminate();
    for (const task of this.pending.values()) task.reject(new Error("Analyzer disposed"));
    this.pending.clear();
  }
}
