import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { createExecHelpers } from './test-utils';
import * as fs from 'fs';
import * as path from 'path';

const LOCAL_E2E_DIR = path.resolve(__dirname);
const LOCAL_DB_DIR = path.join(LOCAL_E2E_DIR, 'test-db-rdf-star');
const WORKSPACE_DIR = path.resolve(__dirname, '..');
const CONTAINER_CWD = '/workspace/e2e-cli/test-db-rdf-star';

describe('QLever CLI RDF* Error Handling', { timeout: 120000 }, () => {
    beforeAll(() => {
        if (fs.existsSync(LOCAL_DB_DIR)) {
            fs.rmSync(LOCAL_DB_DIR, { recursive: true, force: true });
        }
        fs.mkdirSync(LOCAL_DB_DIR, { recursive: true });

        // Write an N-Triples file containing RDF* syntax (embedded triple as subject).
        const rdfStarData =
            '<< <http://example.org/s> <http://example.org/p> <http://example.org/o> >>' +
            ' <http://example.org/p2> <http://example.org/o2> .\n';
        fs.writeFileSync(path.join(LOCAL_DB_DIR, 'rdf-star.nt'), rdfStarData);

        const config = {
            index_name: 'test-index',
            index_directory: CONTAINER_CWD,
            input_files: [{ path: `${CONTAINER_CWD}/rdf-star.nt`, format: 'nt' }],
        };
        fs.writeFileSync(
            path.join(LOCAL_DB_DIR, 'build-config.json'),
            JSON.stringify(config),
        );
    });

    afterAll(() => {
        if (fs.existsSync(LOCAL_DB_DIR)) {
            fs.rmSync(LOCAL_DB_DIR, { recursive: true, force: true });
        }
    });

    const { execDockerRaw } = createExecHelpers(CONTAINER_CWD, WORKSPACE_DIR);

    it('should fail with a clear RDF* error message when input contains embedded triples', () => {
        const configData = fs.readFileSync(
            path.join(LOCAL_DB_DIR, 'build-config.json'),
            'utf-8',
        );
        const { exitCode, stdout, stderr } = execDockerRaw(
            `/workspace/qlever-cli build-index '${configData.replace(/'/g, "'\\''")}'`,
        );

        expect(exitCode).not.toBe(0);

        // The error JSON is printed to stdout; the RDF* message may also appear in
        // the log output on stderr. Check both.
        const combined = stdout + stderr;
        expect(combined).toContain('RDF*');
    });
});
