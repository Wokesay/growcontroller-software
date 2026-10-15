// SPDX-License-Identifier: AGPL-3.0-or-later
// Types of scripts/tool-modules.mjs for vite.config.ts.
import type { Plugin } from "vite";

type Lock = { packages?: Record<string, { dev?: boolean }> };

export function shippedProblems(ids: string[], root: string, lock: Lock): string[];
export function toolCodeGuard(options?: { root?: string; lock?: Lock }): Plugin;
