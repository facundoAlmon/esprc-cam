const gulp = require('gulp');
const htmlmin = require('gulp-htmlmin');
const inlineSource = require('gulp-inline-source');
const replace = require('gulp-replace');
const del = require('del');
const { rollup } = require('rollup');
const resolve = require('@rollup/plugin-node-resolve').default;
const terser = require('@rollup/plugin-terser');

const paths = {
    html: 'src/index.html',
    js_entry: 'src/script.js',
    css: 'src/style.css',
    dest: 'build',
    mainDest: '../main'
};

const clean = () => del([paths.dest], { force: true });

const scripts = async () => {
    const bundle = await rollup({
        input: paths.js_entry,
        plugins: [resolve(), terser()]
    });
    return bundle.write({
        file: `${paths.dest}/script.js`,
        format: 'iife',
        sourcemap: false
    });
};

const copyAssets = () => gulp.src([paths.html, paths.css]).pipe(gulp.dest(paths.dest));

const buildHtml = () => {
    return gulp.src(`${paths.dest}/index.html`)
        .pipe(replace('<script src="script.js" type="module" inline></script>', '<script src="script.js" inline></script>'))
        .pipe(inlineSource({ rootpath: paths.dest, compress: false }))
        .pipe(htmlmin({ collapseWhitespace: true, removeComments: true, minifyCSS: true, minifyJS: false }))
        .pipe(gulp.dest(paths.dest))
        .pipe(gulp.dest(paths.mainDest));
};

const cleanBuild = () => del([`${paths.dest}/script.js`, `${paths.dest}/style.css`], { force: true });

const build = gulp.series(clean, gulp.parallel(scripts, copyAssets), buildHtml, cleanBuild);

exports.clean = clean;
exports.build = build;
exports.default = build;
