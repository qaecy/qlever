import { describe, it, expect, beforeAll } from 'vitest';
import { execSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';

// Regression test: after an upstream merge, File.h lost its openFromFilePointer
// method and ParallelBuffer.cpp stopped mapping "-" to stdin. This caused
// build-index with path "-" to fail with:
//   ERROR opening file "/dev/stdin" with mode "r" (No such device or address)
// inside Docker on Alpine (where /dev/stdin is not available as a device file).
//
// Required fixes (re-apply after every upstream merge):
//   src/util/File.h              — public openFromFilePointer(FILE*) method
//   src/parser/ParallelBuffer.cpp — open() must branch on filename == "-"

const IMAGE_NAME = 'qlever-cli:alpine-test';

const LOCAL_E2E_DIR = path.resolve(__dirname);
const LOCAL_DB_DIR = path.join(LOCAL_E2E_DIR, 'test-db-stdin');
const WORKSPACE_DIR = path.resolve(__dirname, '..');
const CONTAINER_DB_BASE = '/workspace/e2e-cli/test-db-stdin/test-index';
const CONTAINER_CWD = '/workspace/e2e-cli/test-db-stdin';

describe('QLever CLI build-index from stdin', { timeout: 120000 }, () => {
    beforeAll(() => {
        if (fs.existsSync(LOCAL_DB_DIR)) {
            fs.rmSync(LOCAL_DB_DIR, { recursive: true, force: true });
        }
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });
    });

    const execDocker = (cmd: string, inputString?: string) => {
        const escapedCmd = cmd.replace(/'/g, "'\\''");
        const fullCmd = `docker run --init --rm --user root ${inputString !== undefined ? '-i' : ''} -v "${WORKSPACE_DIR}":/workspace -w ${CONTAINER_CWD} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd} && sync'`;
        try {
            return execSync(fullCmd, {
                encoding: 'utf-8',
                cwd: WORKSPACE_DIR,
                input: inputString !== undefined ? inputString : undefined,
                maxBuffer: 10 * 1024 * 1024,
                timeout: 300000,
            });
        } catch (error: any) {
            console.error(`Error running command: ${fullCmd}`);
            console.error(`Stdout: ${error.stdout}`);
            console.error(`Stderr: ${error.stderr}`);
            throw error;
        }
    };

    it('should build an NT index when input path is "-" (stdin)', () => {
        const ntData = '<http://example.org/s1> <http://example.org/p1> <http://example.org/o1> .\n';

        // Pass "-" as the file path — the parser must read from stdin, not open
        // a literal file called "-" or "/dev/stdin".
        const config = JSON.stringify({
            index_name: 'test-index',
            index_directory: CONTAINER_CWD,
            input_files: [{ path: '-', format: 'nt' }],
        });

        const out = execDocker(`/qlever/qlever-cli build-index '${config}'`, ntData);
        expect(out).toContain('Index built successfully');
    });

    it('should query the index built from stdin', () => {
        const out = execDocker(
            `/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { ?s ?p ?o }" csv`
        );
        const lines = out.trim().split('\n');
        expect(lines.length).toBe(2); // header + 1 row
        expect(lines[0]).toBe('s,p,o');
        expect(lines).toContain(
            'http://example.org/s1,http://example.org/p1,http://example.org/o1'
        );
    });

    it('should build an NQ index when input path is "-" (stdin)', () => {
        // Clean up previous index for a fresh quad build
        if (fs.existsSync(LOCAL_DB_DIR)) {
            fs.rmSync(LOCAL_DB_DIR, { recursive: true, force: true });
        }
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });

        const nqData =
            '<http://example.org/s2> <http://example.org/p2> <http://example.org/o2> <http://example.org/g2> .\n';

        const config = JSON.stringify({
            index_name: 'test-index',
            index_directory: CONTAINER_CWD,
            input_files: [{ path: '-', format: 'nq' }],
        });

        const out = execDocker(`/qlever/qlever-cli build-index '${config}'`, nqData);
        expect(out).toContain('Index built successfully');
    });

    it('should query the NQ index built from stdin (named graph)', () => {
        const out = execDocker(
            `/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { GRAPH <http://example.org/g2> { ?s ?p ?o } }" csv`
        );
        const lines = out.trim().split('\n');
        expect(lines.length).toBe(2); // header + 1 row
        expect(lines[0]).toBe('s,p,o');
        expect(lines).toContain(
            'http://example.org/s2,http://example.org/p2,http://example.org/o2'
        );
    });
});
