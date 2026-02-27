import { describe, it, expect, beforeAll } from 'vitest';
import { execSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';

// Using the container image defined in README or from user's latest local build
const IMAGE_NAME = 'qlever-cli:alpine-test';

// Local and container paths
const LOCAL_E2E_DIR = path.resolve(__dirname);
const LOCAL_DB_DIR = path.join(LOCAL_E2E_DIR, 'test-db-quads');
const WORKSPACE_DIR = path.resolve(__dirname, '..');
// Path inside docker container (mounted to /workspace)
const CONTAINER_DB_BASE = '/workspace/e2e-cli/test-db-quads/test-index';
// Working dir inside the container: use the DB dir so temp files
// (e.g. .tripleBufferForPatterns.dat) are isolated per test suite
// and don't collide when quads and triples tests run in parallel.
const CONTAINER_CWD = '/workspace/e2e-cli/test-db-quads';

describe('QLever CLI E2E Flow Quads', { timeout: 120000 }, () => {
    beforeAll(() => {
        // 1. Clean up previous db if any
        if (fs.existsSync(LOCAL_DB_DIR)) {
            fs.rmSync(LOCAL_DB_DIR, { recursive: true, force: true });
        }
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });

        // 2. Create initial data to build the index from
        const initialData = '<http://example.org/initial> <http://example.org/p> <http://example.org/o> <http://example.org/g> .\n';
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'initial.nq'), initialData);

        // 3. Create the build index config
        const config = {
            index_name: "test-index",
            index_directory: CONTAINER_CWD,
            input_files: [
                { path: path.join(CONTAINER_CWD, 'initial.nq'), format: "nq" }
            ]
        };
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'build-config.json'), JSON.stringify(config));
    });

    const execDocker = (cmd: string, inputString?: string) => {
        // Escape single quotes for the sh -c argument
        const escapedCmd = cmd.replace(/'/g, "'\\''");
        const fullCmd = `docker run --init --rm --user root ${inputString ? '-i' : ''} -v "${WORKSPACE_DIR}":/workspace -w ${CONTAINER_CWD} --entrypoint="" ${IMAGE_NAME} sh -c '${escapedCmd} && sync'`;
        try {
            return execSync(fullCmd, {
                encoding: 'utf-8',
                cwd: WORKSPACE_DIR,
                input: inputString ? inputString : undefined,
                maxBuffer: 10 * 1024 * 1024, // 10MB
                timeout: 300000 // 5 minutes
            });
        } catch (error: any) {
            console.error(`Error running command: ${fullCmd}`);
            console.error(`Stdout: ${error.stdout}`);
            console.error(`Stderr: ${error.stderr}`);
            throw error;
        }
    };

    it('should build the index', () => {
        const configData = fs.readFileSync(path.join(LOCAL_DB_DIR, 'build-config.json'), 'utf-8');
        // Using single quotes means double quotes are safe, but we must escape single quotes as needed
        const escapedConfig = configData;
        const out = execDocker(`/qlever/qlever-cli build-index '${escapedConfig.replace(/'/g, "'\\''")}'`);
        expect(out).toContain('Index built successfully');
    });

    it('should query the initial data in default graph', () => {
        // Request csv format to make it easy to assert
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { ?s ?p ?o }" csv`);
        const lines = out.trim().split('\n');
        expect(lines.length).toBeGreaterThanOrEqual(2); // Header + at least 1 row
        expect(lines[0]).toBe('s,p,o');
        expect(lines).toContain('http://example.org/initial,http://example.org/p,http://example.org/o');
    });

    it('should query the initial data in named graph', () => {
        // Request csv format to make it easy to assert
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { GRAPH <http://example.org/g> { ?s ?p ?o } }" csv`);
        const lines = out.trim().split('\n');
        expect(lines.length).toBeGreaterThanOrEqual(2); // Header + at least 1 row
        expect(lines[0]).toBe('s,p,o');
        expect(lines).toContain('http://example.org/initial,http://example.org/p,http://example.org/o');
    });

    it('should insert new data via stream (write command)', () => {
        const newData = '<http://example.org/inserted> <http://example.org/p> <http://example.org/o> <http://example.org/g2> .\n';
        const out = execDocker(`/qlever/qlever-cli write ${CONTAINER_DB_BASE} nq -`, newData);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.message).toContain('Inserted 1 triples successfully');
    });

    it('should query to confirm insertion in default graph', () => {
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { ?s ?p ?o }" csv`);
        const lines = out.trim().split('\n');
        // Header + initial + inserted = 3 lines
        expect(lines.length).toBe(3);
        const csvContent = out.trim();
        expect(csvContent).toContain('http://example.org/initial,http://example.org/p,http://example.org/o');
        expect(csvContent).toContain('http://example.org/inserted,http://example.org/p,http://example.org/o');
    });

    it('should query to confirm insertion in named graph', () => {
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { GRAPH <http://example.org/g2> { ?s ?p ?o } }" csv`);
        const lines = out.trim().split('\n');
        // Header + inserted = 2 lines
        expect(lines.length).toBe(2);
        const csvContent = out.trim();
        expect(csvContent).not.toContain('http://example.org/initial,http://example.org/p,http://example.org/o');
        expect(csvContent).toContain('http://example.org/inserted,http://example.org/p,http://example.org/o');
    });

    it('should delete the original data via stream (delete command)', () => {
        const deleteData = '<http://example.org/initial> <http://example.org/p> <http://example.org/o> <http://example.org/g> .\n';
        const out = execDocker(`/qlever/qlever-cli delete ${CONTAINER_DB_BASE} nq -`, deleteData);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.message).toContain('Deleted 1 triples successfully');
    });

    it('should query and conclude that only the inserted data remains in default graph', () => {
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { ?s ?p ?o }" csv`);
        const lines = out.trim().split('\n');
        console.log("YYY")
        console.log(lines);
        // Header + inserted = 2 lines
        expect(lines.length).toBe(2);
        expect(lines).not.toContain('http://example.org/initial,http://example.org/p,http://example.org/o');
        expect(lines).toContain('http://example.org/inserted,http://example.org/p,http://example.org/o');
    });

    it('should query and conclude that only the inserted data remains in named graph', () => {
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s ?p ?o WHERE { GRAPH <http://example.org/g2> { ?s ?p ?o } }" csv`);
        const lines = out.trim().split('\n');
        // Header + inserted = 2 lines
        expect(lines.length).toBe(2);
        expect(lines).not.toContain('http://example.org/initial,http://example.org/p,http://example.org/o');
        expect(lines).toContain('http://example.org/inserted,http://example.org/p,http://example.org/o');
    });

    it('should add a triple via SPARQL UPDATE', () => {
        const updateQuery = 'INSERT DATA { GRAPH <http://example.org/g3> { <http://example.org/s3> <http://example.org/p3> <http://example.org/o3> } }';
        const out = execDocker(`/qlever/qlever-cli update ${CONTAINER_DB_BASE} "${updateQuery}"`);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);

        // Verify
        const verifyOut = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s WHERE { GRAPH <http://example.org/g3> { ?s ?p ?o } }" csv`);
        expect(verifyOut).toContain('http://example.org/s3');
    });

    it('should delete a triple via SPARQL UPDATE', () => {
        const updateQuery = 'DELETE DATA { GRAPH <http://example.org/g3> { <http://example.org/s3> <http://example.org/p3> <http://example.org/o3> } }';
        const out = execDocker(`/qlever/qlever-cli update ${CONTAINER_DB_BASE} "${updateQuery}"`);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);

        // Verify
        const verifyOut = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s WHERE { GRAPH <http://example.org/g3> { ?s ?p ?o } }" csv`);
        const lines = verifyOut.trim().split('\n');
        expect(lines.length).toBe(1); // Only header
    });

    it('should write a triple and specify graph using a flag', () => {
        const triple = '<http://example.org/s4> <http://example.org/p4> <http://example.org/o4> .\n';
        // Note: using nt format since nq would specify graph in-line
        const out = execDocker(`/qlever/qlever-cli write ${CONTAINER_DB_BASE} nt - --graph http://example.org/g4`, triple);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);

        // Verify
        const verifyOut = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE} "SELECT ?s WHERE { GRAPH <http://example.org/g4> { ?s ?p ?o } }" csv`);
        expect(verifyOut).toContain('http://example.org/s4');
    });

    it('should execute binary-rebuild successfully', () => {
        const out = execDocker(`/qlever/qlever-cli binary-rebuild ${CONTAINER_DB_BASE} ${CONTAINER_DB_BASE}.rebuilt`);
        const result = JSON.parse(out);
        expect(result.success).toBe(true);
        expect(result.message).toContain('Binary rebuild completed successfully');
    });

    it('should still have the correct data after binary-rebuild', () => {
        const out = execDocker(`/qlever/qlever-cli query ${CONTAINER_DB_BASE}.rebuilt "SELECT ?s ?p ?o WHERE { ?s ?p ?o }" csv`);
        const lines = out.trim().split('\n');
        // Header + inserted + s4 = 3 lines (since initial was deleted before rebuild)
        expect(lines.length).toBe(3);
        expect(lines).toContain('http://example.org/inserted,http://example.org/p,http://example.org/o');
        expect(lines).toContain('http://example.org/s4,http://example.org/p4,http://example.org/o4');
    });
});
