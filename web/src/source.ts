// SPDX-License-Identifier: AGPL-3.0-or-later
// Where the source code of this build lives (AGPL-3.0 §13): the repository
// and the commit or tag the web app was built from. vite.config.ts sets the
// revision at build time; CI and release builds fail without one.
declare const __GC_SOURCE_REV__: string;

export const SOURCE_REPO = "https://github.com/Wokesay/growcontroller-software";
export const sourceRev: string = __GC_SOURCE_REV__;
/** Is the exact revision of this build known? */
export const sourceKnown: boolean = sourceRev !== "";
export const sourceUrl: string = sourceKnown ? `${SOURCE_REPO}/tree/${sourceRev}` : SOURCE_REPO;
/** Short form for display: a tag as it is, a commit hash with 12 characters. */
export const sourceLabel: string = /^[0-9a-f]{40}$/.test(sourceRev) ? sourceRev.slice(0, 12) : sourceRev;
