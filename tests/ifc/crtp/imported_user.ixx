export module reflgen.test.imported_user;
import reflgen.test.imported_base;
export namespace game {
    struct settings { int enabled = 0; };
    struct render_settings : reflgen::reflected_base<render_settings, settings> { float exposure = 0; };
}
