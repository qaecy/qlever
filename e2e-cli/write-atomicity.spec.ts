import { describe, it, expect, beforeAll } from 'vitest';
import { createExecHelpers } from './test-utils';
import * as fs from 'fs';
import * as path from 'path';
import * as crypto from 'crypto';
import { execSync } from 'child_process';

/**
 * `write` / `delete` must be all-or-nothing.
 *
 * THE BUG THIS COVERS: the parser hands the command one block of at most
 * `DEFAULT_PARSER_BUFFER_SIZE` (10 MB) at a time, and the command used to apply
 * each block in its own `DeltaTriplesManager::modify` call — which serializes the
 * whole delta to `.update-triples` before returning. So an input larger than 10 MB
 * committed its blocks as it went: a parse error in block N left blocks 0..N-1
 * persisted while the command exited non-zero and printed an error. The caller had
 * no way to know, and `POST /data` reported the write as failed while part of the
 * payload was in the index. Measured on the pre-fix binary: a failed 12.7 MB write
 * grew the delta from 277 bytes to 13,500,323.
 *
 * The whole parse loop now runs inside ONE `modify`, so nothing is written back
 * until the last block has parsed successfully.
 *
 * The input has to exceed 10 MB for this to test anything at all — a smaller one
 * is a single block and was always atomic. That is also why this lives in its own
 * spec file rather than in `extended-commands.spec.ts`: it needs a throwaway index
 * and a ~13 MB fixture.
 */

const LOCAL_E2E_DIR = path.resolve(__dirname);
const LOCAL_DB_DIR = path.join(LOCAL_E2E_DIR, 'test-db-atomicity');
const WORKSPACE_DIR = path.resolve(__dirname, '..');
const CONTAINER_DB_BASE = '/workspace/e2e-cli/test-db-atomicity/test-index';
const CONTAINER_CWD = '/workspace/e2e-cli/test-db-atomicity';

/** Comfortably more than one 10 MB parser block. */
const BIG_TRIPLE_COUNT = 120_000;

const deltaPath = path.join(LOCAL_DB_DIR, 'test-index.update-triples');

function deltaFingerprint(): { md5: string; size: number } {
    const bytes = fs.readFileSync(deltaPath);
    return {
        md5: crypto.createHash('md5').update(bytes).digest('hex'),
        size: bytes.length,
    };
}

describe('QLever CLI write atomicity', { timeout: 300000 }, () => {
    const { execDocker, execDockerRaw } = createExecHelpers(CONTAINER_CWD, WORKSPACE_DIR);

    beforeAll(() => {
        // See the note in extended-commands.spec.ts: fs.rmSync hits ENOTEMPTY on
        // Alpine, and a bind-mounted macOS host can drop a .DS_Store in mid-removal.
        for (let attempt = 0; attempt < 5; attempt++) {
            try {
                execSync(`rm -rf "${LOCAL_DB_DIR}"`);
                break;
            } catch {
                if (attempt === 4) throw new Error(`Could not clear ${LOCAL_DB_DIR}`);
            }
        }
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });

        fs.writeFileSync(
            path.join(LOCAL_DB_DIR, 'initial.nt'),
            '<http://example.org/s1> <http://example.org/p1> <http://example.org/o1> .\n',
        );
        const config = {
            index_name: 'test-index',
            index_directory: CONTAINER_CWD,
            input_files: [{ path: `${CONTAINER_CWD}/initial.nt`, format: 'nt' }],
        };
        const out = execDocker(
            `/workspace/qlever-cli build-index '${JSON.stringify(config).replace(/'/g, "'\\''")}'`,
        );
        expect(out).toContain('Index built successfully');

        // A committed delta, so the failing write below has prior state to damage.
        // Without this the test would only prove that nothing was created, which is
        // a much weaker claim than "what was already there is untouched".
        fs.writeFileSync(
            path.join(LOCAL_DB_DIR, 'good.nt'),
            '<http://example.org/a> <http://example.org/b> <http://example.org/c> .\n',
        );
        execDocker(`/workspace/qlever-cli write ${CONTAINER_DB_BASE} nt ${CONTAINER_CWD}/good.nt`);
        expect(fs.existsSync(deltaPath)).toBe(true);

        // ~13 MB of valid N-Triples followed by an unterminated string literal.
        // The literal matters: most malformed N-Triples (a bad IRI, a missing final
        // dot, an empty langtag) are silently tolerated by the parser and produce a
        // SUCCESSFUL write, so they cannot drive this test. An unterminated literal
        // is one of the few inputs that actually throws.
        const lines: string[] = [];
        for (let i = 0; i < BIG_TRIPLE_COUNT; i++) {
            const n = String(i).padStart(8, '0');
            lines.push(
                `<http://example.org/subject${n}> <http://example.org/predicate> <http://example.org/object${n}> .`,
            );
        }
        lines.push('<http://example.org/x> <http://example.org/y> "unterminated .');
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'big-then-broken.nt'), `${lines.join('\n')}\n`);
    });

    it('spans more than one parser block, or the test proves nothing', () => {
        const size = fs.statSync(path.join(LOCAL_DB_DIR, 'big-then-broken.nt')).size;
        expect(size).toBeGreaterThan(10 * 1024 * 1024);
    });

    it('leaves the delta byte-identical when a multi-block write fails', () => {
        const before = deltaFingerprint();

        const { exitCode, stderr } = execDockerRaw(
            `/workspace/qlever-cli write ${CONTAINER_DB_BASE} nt ${CONTAINER_CWD}/big-then-broken.nt`,
        );
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('Unterminated string literal');

        const after = deltaFingerprint();
        // Byte-identical, not merely "same triple count": a partial write also
        // reorders and re-serializes, so the md5 is the strict check and the size
        // is what makes a failure readable.
        expect(after.size).toBe(before.size);
        expect(after.md5).toBe(before.md5);
    });

    it('still has exactly the pre-failure triples', () => {
        const out = execDocker(
            `/workspace/qlever-cli query ${CONTAINER_DB_BASE} ` +
            `"SELECT (COUNT(*) AS ?c) WHERE { ?s ?p ?o }" csv`,
        );
        // 1 from the base index + 1 from the committed delta. None of the 120k
        // triples in the failed write may have survived.
        expect(out).toMatch(/(^|\n)2(\n|$)/);
        expect(out).not.toMatch(/12000[0-9]/);
    });

    it('accepts a valid multi-block write, so the atomicity fix did not break the happy path', () => {
        const lines: string[] = [];
        for (let i = 0; i < BIG_TRIPLE_COUNT; i++) {
            const n = String(i).padStart(8, '0');
            lines.push(
                `<http://example.org/ok${n}> <http://example.org/predicate> <http://example.org/object${n}> .`,
            );
        }
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'big-valid.nt'), `${lines.join('\n')}\n`);

        const out = execDocker(
            `/workspace/qlever-cli write ${CONTAINER_DB_BASE} nt ${CONTAINER_CWD}/big-valid.nt`,
        );
        expect(out).toContain(`Inserted ${BIG_TRIPLE_COUNT} triples successfully`);

        const count = execDocker(
            `/workspace/qlever-cli query ${CONTAINER_DB_BASE} ` +
            `"SELECT (COUNT(*) AS ?c) WHERE { ?s ?p ?o }" csv`,
        );
        expect(count).toContain(String(BIG_TRIPLE_COUNT + 2));
    });

    it('is atomic for `delete` too', () => {
        const before = deltaFingerprint();
        const { exitCode } = execDockerRaw(
            `/workspace/qlever-cli delete ${CONTAINER_DB_BASE} nt ${CONTAINER_CWD}/big-then-broken.nt`,
        );
        expect(exitCode).not.toBe(0);
        const after = deltaFingerprint();
        expect(after.md5).toBe(before.md5);
    });
});
