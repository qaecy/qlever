import { describe, it, expect, beforeAll } from 'vitest';
import { execSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';

const IMAGE_NAME = 'qlever-cli:alpine-test';
const LOCAL_E2E_DIR = path.resolve(__dirname);
const LOCAL_DB_DIR = path.join(LOCAL_E2E_DIR, 'test-db-extended');
const WORKSPACE_DIR = path.resolve(__dirname, '..');
const CONTAINER_DB_BASE = '/workspace/e2e-cli/test-db-extended/test-index';
const CONTAINER_CWD = '/workspace/e2e-cli/test-db-extended';

describe('QLever CLI Extended Commands', { timeout: 120000 }, () => {
    beforeAll(() => {
        if (fs.existsSync(LOCAL_DB_DIR)) {
            fs.rmSync(LOCAL_DB_DIR, { recursive: true, force: true });
        }
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });

        const initialData =
            '<http://example.org/s1> <http://example.org/p1> <http://example.org/o1> .\n' +
            '<http://example.org/s2> <http://example.org/p2> "hello world" .\n' +
            '<http://example.org/s3> <http://example.org/p3> <http://example.org/o3> .\n';
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'initial.nt'), initialData);

        const config = {
            index_name: "test-index",
            index_directory: CONTAINER_CWD,
            input_files: [
                { path: path.join(CONTAINER_CWD, 'initial.nt'), format: "nt" }
            ]
        };
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'build-config.json'), JSON.stringify(config));
    });

    const execDocker = (cmd: string, inputString?: string) => {
        const escapedCmd = cmd.replace(/'/g, "'\\''");
        const fullCmd = `docker run --init --rm --user root ${inputString !== undefined ? '-i' : ''} -v "${WORKSPACE_DIR}":/workspace -w ${CONTAINER_CWD} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd} && sync'`;
        const out = execSync(fullCmd, {
            encoding: 'utf-8',
            cwd: WORKSPACE_DIR,
            input: inputString !== undefined ? inputString : undefined,
            maxBuffer: 10 * 1024 * 1024,
        });
        return out;
    };

    // Helper that captures exit code + stdout + stderr instead of throwing
    const execDockerRaw = (cmd: string, inputString?: string) => {
        const escapedCmd = cmd.replace(/'/g, "'\\''");
        const fullCmd = `docker run --init --rm --user root ${inputString !== undefined ? '-i' : ''} -v "${WORKSPACE_DIR}":/workspace -w ${CONTAINER_CWD} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd}'`;
        try {
            const stdout = execSync(fullCmd, {
                encoding: 'utf-8',
                cwd: WORKSPACE_DIR,
                input: inputString !== undefined ? inputString : undefined,
                maxBuffer: 10 * 1024 * 1024,
            });
            return { exitCode: 0, stdout, stderr: '' };
        } catch (error: any) {
            return {
                exitCode: error.status ?? 1,
                stdout: error.stdout ?? '',
                stderr: error.stderr ?? '',
            };
        }
    };

    it('should build the index', () => {
        const configData = fs.readFileSync(path.join(LOCAL_DB_DIR, 'build-config.json'), 'utf-8');
        const out = execDocker(`/qlever/qlever-cli build-index '${configData.replace(/'/g, "'\\''")}'`);
        expect(out).toContain('Index built successfully');
    });

    // ── stats ──────────────────────────────────────────────────

    it('should return stats as valid JSON', () => {
        const out = execDocker(`/qlever/qlever-cli stats ${CONTAINER_DB_BASE}`);
        const result = JSON.parse(out);
        expect(result.indexBasename).toContain('test-index');
        expect(result.numTriples).toBeGreaterThanOrEqual(3);
    });

    // ── query-json ─────────────────────────────────────────────

    it('should execute a query via query-json', () => {
        const input = JSON.stringify({
            indexBasename: CONTAINER_DB_BASE,
            query: "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o }",
            format: "csv"
        });
        const out = execDocker(`/qlever/qlever-cli query-json '${input.replace(/'/g, "'\\''")}'`);
        expect(out).toContain('count');
    });

    it('should fail query-json with missing fields', () => {
        const input = JSON.stringify({ query: "SELECT * WHERE { ?s ?p ?o }" });
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli query-json '${input.replace(/'/g, "'\\''")}'`);
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('Missing required fields');
    });

    it('should fail query-json with invalid JSON', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli query-json 'not valid json'`);
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('Invalid JSON');
    });

    // ── serialize ──────────────────────────────────────────────

    it('should serialize the index as nt to stdout', () => {
        const out = execDocker(`/qlever/qlever-cli serialize ${CONTAINER_DB_BASE} nt`);
        expect(out).toContain('<http://example.org/s1>');
        expect(out).toContain('<http://example.org/p1>');
        expect(out).toContain('<http://example.org/o1>');
    });

    it('should serialize the index as nt to a file', () => {
        const outputFile = `${CONTAINER_CWD}/serialized.nt`;
        const localFile = path.join(LOCAL_DB_DIR, 'serialized.nt');
        execDocker(`/qlever/qlever-cli serialize ${CONTAINER_DB_BASE} nt ${outputFile}`);
        const contents = fs.readFileSync(localFile, 'utf-8');
        expect(contents).toContain('<http://example.org/s1>');
        expect(contents).toContain('"hello world"');
    });

    it('should serialize the index as nq to stdout', () => {
        const out = execDocker(`/qlever/qlever-cli serialize ${CONTAINER_DB_BASE} nq`);
        expect(out).toContain('<http://example.org/s1>');
    });

    // ── query-to-file ──────────────────────────────────────────

    it('should execute CONSTRUCT query to file', () => {
        const outputFile = `${CONTAINER_CWD}/construct-result.nt`;
        const localFile = path.join(LOCAL_DB_DIR, 'construct-result.nt');
        const query = 'CONSTRUCT { ?s ?p ?o } WHERE { ?s ?p ?o }';
        const out = execDocker(`/qlever/qlever-cli query-to-file ${CONTAINER_DB_BASE} "${query}" nt ${outputFile}`);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.outputFile).toBe(outputFile);

        const contents = fs.readFileSync(localFile, 'utf-8');
        expect(contents).toContain('<http://example.org/s1>');
    });

    // ── query with CONSTRUCT ───────────────────────────────────

    it('should execute CONSTRUCT query to stdout', () => {
        const query = 'CONSTRUCT { ?s ?p ?o } WHERE { ?s ?p ?o } LIMIT 10';
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "${query}" nt`);
        expect(out).toContain('<http://example.org/s1>');
        expect(out).toContain('<http://example.org/p1>');
    });

    // ── write/delete edge cases ────────────────────────────────

    it('should handle empty stdin for write (0 triples)', () => {
        const out = execDocker(`/qlever/qlever-cli write ${CONTAINER_DB_BASE} nt -`, '');
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.message).toContain('Inserted 0 triples');
    });

    it('should handle empty stdin for delete (0 triples)', () => {
        const out = execDocker(`/qlever/qlever-cli delete ${CONTAINER_DB_BASE} nt -`, '');
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.message).toContain('Deleted 0 triples');
    });

    it('should fail write with unsupported format', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli write ${CONTAINER_DB_BASE} xml -`, '');
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('Unsupported format');
    });

    // ── binary-rebuild skip path ───────────────────────────────

    it('should skip binary-rebuild when no deltas exist', () => {
        // Build a fresh index with no modifications
        const freshDir = `${CONTAINER_CWD}/fresh`;
        const localFreshDir = path.join(LOCAL_DB_DIR, 'fresh');
        fs.mkdirSync(localFreshDir, { recursive: true });
        fs.writeFileSync(path.join(localFreshDir, 'data.nt'),
            '<http://example.org/a> <http://example.org/b> <http://example.org/c> .\n');
        const config = JSON.stringify({
            index_name: "fresh-index",
            index_directory: freshDir,
            input_files: [{ path: `${freshDir}/data.nt`, format: "nt" }]
        });
        execDocker(`/qlever/qlever-cli build-index '${config.replace(/'/g, "'\\''")}'`);

        const out = execDocker(`/qlever/qlever-cli binary-rebuild ${freshDir}/fresh-index`);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.skipped).toBe(true);
        expect(result.message).toContain('not necessary');
    });

    // ── error handling ─────────────────────────────────────────

    it('should fail gracefully with non-existent index for query', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli query ${CONTAINER_CWD}/does-not-exist "SELECT * WHERE { ?s ?p ?o }"`);
        expect(exitCode).not.toBe(0);
        expect(stderr.length).toBeGreaterThan(0);
    });

    it('should fail gracefully with non-existent index for stats', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli stats ${CONTAINER_CWD}/does-not-exist`);
        expect(exitCode).not.toBe(0);
        expect(stderr.length).toBeGreaterThan(0);
    });

    it('should fail gracefully with invalid format for query', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT * WHERE { ?s ?p ?o }" xml`);
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('Unsupported format');
    });

    it('should fail gracefully with invalid format for serialize', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli serialize ${CONTAINER_DB_BASE} ttl`);
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('only supports nt and nq');
    });

    // ── help / unknown command ─────────────────────────────────

    it('should print help and exit 0', () => {
        const { exitCode, stdout } = execDockerRaw(`/qlever/qlever-cli --help`);
        expect(exitCode).toBe(0);
        expect(stdout).toContain('Commands:');
    });

    it('should fail with unknown command', () => {
        const { exitCode, stderr } = execDockerRaw(`/qlever/qlever-cli nonsense`);
        expect(exitCode).not.toBe(0);
        expect(stderr).toContain('Commands:');
    });
});
