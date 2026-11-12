import { defineConfig } from 'vite';
import { serve } from './vite.serve.ts';

// 5173, and the rodeo-demo editor 5174, so both can run at once at fixed addresses.
export default defineConfig({ ...serve(5173) });
