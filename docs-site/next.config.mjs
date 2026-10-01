import { createMDX } from 'fumadocs-mdx/next';

const withMDX = createMDX();

/** @type {import('next').NextConfig} */
const config = {
  output: 'export',
  reactStrictMode: true,
  basePath: '/cforge',
  trailingSlash: true,
  env: {
    NEXT_PUBLIC_SITE_URL: 'https://sumesh-s-dev.github.io/cforge',
  },
};

export default withMDX(config);
