import { defineConfig } from 'vite';
import { serve } from '../../../tools/rodeo-console/vite.serve.ts';

// 5174: the console takes 5173, so both can run at once at fixed addresses.
export default defineConfig({ ...serve(5174) });
