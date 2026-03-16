/**
 * Large-file performance benchmark for qlever-cli query execution.
 *
 * Downloads nest_explicit.nq.gz from GCS (cached on disk between runs), builds
 * a QLever index from it, then runs a set of SPARQL queries measuring
 * wall-clock elapsed time for each invocation.  The QLever engine prints a
 * "Sorting random result tables …" line to stderr on every startup; we capture
 * that too so the report makes the startup overhead visible separately from the
 * actual query work.
 *
 * The test never hard-fails on timing thresholds – it is purely informational.
 * A JSON report is written to test-db-perf-large/performance-report.json and a
 * human-readable summary is printed via console.log at the end.
 *
 * Prerequisites on the host: curl (macOS built-in) or gsutil (Google Cloud SDK).
 */

import { describe, it, expect, beforeAll } from 'vitest';
import { spawnSync, execSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';

// ── constants ──────────────────────────────────────────────────────────────

const IMAGE_NAME = 'qlever-cli:alpine-test';
const LOCAL_E2E_DIR = path.resolve(__dirname);
const LOCAL_DB_DIR = path.join(LOCAL_E2E_DIR, 'test-db-perf-large');
const WORKSPACE_DIR = path.resolve(__dirname, '..');
const CONTAINER_DB_BASE = '/workspace/e2e-cli/test-db-perf-large/nest-index';
const CONTAINER_CWD = '/workspace/e2e-cli/test-db-perf-large';

const GCS_BUCKET = 'cue_public_eu_west6';
const GCS_OBJECT = 'test-data/nest_explicit.nq.gz';
const GCS_URI    = `gs://${GCS_BUCKET}/${GCS_OBJECT}`;
const HTTPS_URL  = `https://storage.googleapis.com/${GCS_BUCKET}/${GCS_OBJECT}`;
const GZ_FILENAME = 'nest_explicit.nq.gz';

// ── helper types ───────────────────────────────────────────────────────────

interface BenchmarkResult {
    label: string;
    query: string;
    format: string;
    elapsedMs: number;
    /** Lines matching the sorting-estimate message, if any. */
    sortingEstimateLines: string[];
    rowsReturned: number | null;
    error: string | null;
}

// ── download helper ────────────────────────────────────────────────────────

function downloadFile(dest: string): void {
    // Prefer gsutil (parallel composite download) when available
    const hasGsutil = spawnSync('which', ['gsutil'], { encoding: 'utf-8' }).status === 0;
    if (hasGsutil) {
        console.log(`  Downloading via gsutil: ${GCS_URI}`);
        execSync(`gsutil cp "${GCS_URI}" "${dest}"`, { stdio: 'inherit' });
        return;
    }
    console.log(`  Downloading via curl: ${HTTPS_URL}`);
    execSync(`curl -fL --progress-bar "${HTTPS_URL}" -o "${dest}"`, { stdio: 'inherit' });
}

// ── docker helpers ─────────────────────────────────────────────────────────

/**
 * Run a command inside the container and always capture both stdout and stderr.
 */
const execDockerRaw = (cmd: string) => {
    const escapedCmd = cmd.replace(/'/g, "'\\''");
    const result = spawnSync(
        'docker',
        [
            'run', '--init', '--rm', '--user', 'root',
            '-v', `${WORKSPACE_DIR}:/workspace`,
            '-w', CONTAINER_CWD,
            '--entrypoint', '',
            IMAGE_NAME,
            'sh', '-c', escapedCmd,
        ],
        {
            encoding: 'utf-8',
            cwd: WORKSPACE_DIR,
            maxBuffer: 20 * 1024 * 1024,
        },
    );
    return {
        exitCode: result.status ?? 1,
        stdout: result.stdout ?? '',
        stderr: result.stderr ?? '',
    };
};

/**
 * Run a long-running build command, streaming all output directly to the
 * terminal (stdio: 'inherit') so build progress is visible.
 */
const execDockerBuild = (cmd: string) => {
    const escapedCmd = cmd.replace(/'/g, "'\\''");
    execSync(
        `docker run --init --rm --user root -v "${WORKSPACE_DIR}":/workspace -w ${CONTAINER_CWD} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd} && sync'`,
        { stdio: 'inherit', cwd: WORKSPACE_DIR },
    );
};

// ── benchmark runner ───────────────────────────────────────────────────────

function runBenchmark(label: string, query: string, format = 'csv'): BenchmarkResult {
    const start = Date.now();
    const { exitCode, stdout, stderr } = execDockerRaw(
        `/qlever/qlever-cli query ${CONTAINER_DB_BASE} "${query.replace(/"/g, '\\"')}" ${format}`,
    );
    const elapsedMs = Date.now() - start;

    const sortingEstimateLines = stderr
        .split('\n')
        .filter((l) => l.includes('Sorting random result tables'));

    let rowsReturned: number | null = null;
    if (exitCode === 0 && stdout.trim().length > 0) {
        if (format === 'csv' || format === 'tsv') {
            // First line is header, rest are data rows
            const lines = stdout.trim().split('\n');
            rowsReturned = Math.max(0, lines.length - 1);
        } else {
            // nt/nq: count non-empty lines
            rowsReturned = stdout.trim().split('\n').filter(Boolean).length;
        }
    }

    return {
        label,
        query,
        format,
        elapsedMs,
        sortingEstimateLines,
        rowsReturned: exitCode === 0 ? rowsReturned : null,
        error: exitCode !== 0 ? stderr.slice(0, 400) : null,
    };
}

// ── benchmark definitions ──────────────────────────────────────────────────

const BENCHMARKS: Array<{ label: string; query: string; format?: string }> = [
    {
        label: 'COUNT all quads',
        query: 'SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o }',
    },
    {
        label: 'Full scan LIMIT 100',
        query: 'SELECT ?s ?p ?o WHERE { ?s ?p ?o } LIMIT 100',
    },
    {
        label: 'COUNT distinct subjects',
        query: 'SELECT (COUNT(DISTINCT ?s) AS ?cnt) WHERE { ?s ?p ?o }',
    },
    {
        label: 'COUNT distinct predicates',
        query: 'SELECT (COUNT(DISTINCT ?p) AS ?cnt) WHERE { ?s ?p ?o }',
    },
    {
        label: 'List named graphs LIMIT 50',
        query: 'SELECT DISTINCT ?g WHERE { GRAPH ?g { ?s ?p ?o } } LIMIT 50',
    },
    {
        label: 'Top 10 graphs by triple count',
        query: 'SELECT ?g (COUNT(*) AS ?count) WHERE { GRAPH ?g { ?s ?p ?o } } GROUP BY ?g ORDER BY DESC(?count) LIMIT 10',
    },
    {
        label: 'Filter literals LIMIT 50',
        query: 'SELECT ?s ?p ?o WHERE { ?s ?p ?o . FILTER(isLiteral(?o)) } LIMIT 50',
    },
    {
        label: 'Two-hop join LIMIT 50',
        query: 'SELECT ?a ?c WHERE { ?a ?p1 ?b . ?b ?p2 ?c } LIMIT 50',
    },
    {
        label: 'CONSTRUCT star pattern LIMIT 20',
        query: 'CONSTRUCT { ?s ?p ?o } WHERE { ?s ?p ?o } LIMIT 20',
        format: 'nt',
    },
];

// ── test suite ─────────────────────────────────────────────────────────────

// Generous timeout: download (variable) + index build (variable) + queries
describe('QLever CLI Performance Benchmarks – Large File', { timeout: 7_200_000 }, () => {
    const results: BenchmarkResult[] = [];
    let fileSizeBytes = 0;

    beforeAll(() => {
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });

        // ── 1. Download the .gz file (skip if already cached) ──────────────
        const localGz = path.join(LOCAL_DB_DIR, GZ_FILENAME);
        if (fs.existsSync(localGz)) {
            console.log(`  Using cached file: ${localGz}`);
        } else {
            downloadFile(localGz);
        }
        fileSizeBytes = fs.statSync(localGz).size;
        console.log(`  File size: ${(fileSizeBytes / 1_000_000).toFixed(1)} MB compressed`);

        // ── 2. Skip index build if it already exists and is non-empty ─────
        const metaFile = path.join(LOCAL_DB_DIR, 'nest-index.meta-data.json');
        if (fs.existsSync(metaFile)) {
            console.log('  Index files found – validating…');
            const val = execDockerRaw(
                `/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o }" csv`,
            );
            if (val.exitCode === 0) {
                const valLines = val.stdout.trim().split('\n');
                // CSV: first line is header, second is the count value
                const countValue = valLines.length >= 2 ? parseInt(valLines[1], 10) : 0;
                if (!isNaN(countValue) && countValue > 0) {
                    console.log(`  Using cached index (${countValue.toLocaleString()} triples).`);
                    return;
                }
            }
            // Index is empty or the query failed – wipe it and rebuild
            console.log('  Cached index appears empty or invalid – deleting and rebuilding…');
            for (const f of fs.readdirSync(LOCAL_DB_DIR)) {
                if (f.startsWith('nest-index.')) {
                    fs.rmSync(path.join(LOCAL_DB_DIR, f));
                }
            }
        }

        // ── 3. Build the index (gunzip piped to stdin) ─────────────────────
        const config = {
            index_name: 'nest-index',
            index_directory: CONTAINER_CWD,
            input_files: [{ path: '-', format: 'nq' }],
        };
        const configJson = JSON.stringify(config).replace(/'/g, "'\\''");
        console.log('  Building index (streaming decompression – may take several minutes)…');
        try {
            execDockerBuild(`gunzip -c '${CONTAINER_CWD}/${GZ_FILENAME}' | /qlever/qlever-cli build-index '${configJson}'`);
        } catch (err: any) {
            throw new Error(`Index build failed. See output above.\n${err.message ?? ''}`);
        }
        console.log('  Index built successfully.');
    }, 7_200_000);

    // Run each benchmark as a separate test so vitest reports them individually
    for (const bench of BENCHMARKS) {
        it(bench.label, () => {
            const result = runBenchmark(bench.label, bench.query, bench.format);
            results.push(result);

            // Only assert that the query did not error out
            expect(result.error, `Query failed: ${result.error}`).toBeNull();
        });
    }

    // After all benchmarks, write the report and print a summary
    it('write performance report', () => {
        // Sort by label for a stable report order
        const sorted = [...results].sort((a, b) =>
            a.label.localeCompare(b.label),
        );

        const report = {
            generatedAt: new Date().toISOString(),
            sourceFile: GZ_FILENAME,
            fileSizeBytes,
            results: sorted.map((r) => ({
                label: r.label,
                format: r.format,
                elapsedMs: r.elapsedMs,
                rowsReturned: r.rowsReturned,
                sortingEstimateDetected: r.sortingEstimateLines.length > 0,
                error: r.error,
            })),
        };

        const reportPath = path.join(LOCAL_DB_DIR, 'performance-report.json');
        fs.writeFileSync(reportPath, JSON.stringify(report, null, 2));

        // Human-readable console summary
        const divider = '─'.repeat(72);
        console.log(`\n${divider}`);
        console.log('  QLever CLI Performance Report – Large File');
        console.log(`  Source    : ${GZ_FILENAME} (${(fileSizeBytes / 1_000_000).toFixed(1)} MB compressed)`);
        console.log(`  Generated : ${report.generatedAt}`);
        console.log(divider);
        console.log(
            `  ${'Query'.padEnd(44)} ${'ms'.padStart(6)}  ${'rows'.padStart(6)}  startup?`,
        );
        console.log(divider);
        for (const r of sorted) {
            const ms = r.elapsedMs.toString().padStart(6);
            const rows =
                r.rowsReturned !== null
                    ? r.rowsReturned.toString().padStart(6)
                    : '  err ';
            const startup = r.sortingEstimateLines.length > 0 ? '  yes' : '  no ';
            const label =
                r.label.length > 44 ? r.label.slice(0, 41) + '...' : r.label.padEnd(44);
            console.log(`  ${label} ${ms}  ${rows}  ${startup}`);
        }
        console.log(divider);
        const total = sorted.reduce((s, r) => s + r.elapsedMs, 0);
        console.log(`  Total wall-clock time : ${total} ms`);
        console.log(
            `  Report written to    : ${path.relative(WORKSPACE_DIR, reportPath)}`,
        );
        console.log(`${divider}\n`);

        expect(fs.existsSync(reportPath)).toBe(true);
    });
});
