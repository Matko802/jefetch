// Built-in ASCII logos. For phase 1 we ship a small set; the full set lands
// in the fidelity pass. Each logo is a list of lines and a base color.

#[derive(Debug, Clone)]
pub struct Logo {
    pub name: &'static str,
    pub color: &'static str,
    pub lines: &'static [&'static str],
}

pub static LOGOS: &[Logo] = &[
    Logo {
        name: "nixos",
        color: "34",
        lines: &[
            "          `:oy/:.`          ",
            "        `.yo+/:-.           ",
            "       -ys+s:               ",
            "       s+  oy`              ",
            "       os` .ss`             ",
            "        :+. :sy.            ",
            "         -+: :ys/.          ",
            "           `:/-.`           ",
        ],
    },
    Logo {
        name: "ubuntu",
        color: "31",
        lines: &[
            "          .-::-::.          ",
            "       .:-.      .::        ",
            "      .:.         `::       ",
            "      ::`          `.`      ",
            "      ::            ..      ",
            "      ::`         `.`       ",
            "       `::.     .:'         ",
            "         `:-...-`           ",
        ],
    },
    Logo {
        name: "arch",
        color: "34",
        lines: &[
            "             /\\            ",
            "            /  \\           ",
            "           / /\\ \\          ",
            "          / /  \\ \\         ",
            "         / /    \\ \\        ",
            "        / /      \\ \\       ",
            "       //         \\\\      ",
            "      //           \\\\      ",
            "     /'\\           /' \\    ",
            "      \\\\          //      ",
            "       \\\\        //       ",
            "        \\\\      //         ",
            "         \\\\    //          ",
            "          \\\\  //           ",
            "           \\\\//            ",
        ],
    },
    Logo {
        name: "fedora",
        color: "34",
        lines: &[
            "          fffffff            ",
            "                ffffffff     ",
            "      ffffffffffff           ",
            "    ffff                      ",
            "  ffff                        ",
            " ffff                          ",
            " fff                            ",
            " fff                             ",
            " fff                              ",
            " ff                                ",
            " ff                                 ",
            "  f                                  ",
        ],
    },
    Logo {
        name: "debian",
        color: "31",
        lines: &[
            "         ,=               ,       ",
            "       =;           =;           ",
            "        =;       =;             ",
            "         =;   =;                ",
            "            ={                   ",
            "             {,                  ",
            "              ;#                 ",
            "              {.                 ",
            "             :{                  ",
            "            ##                   ",
        ],
    },
    Logo {
        name: "linux",
        color: "34",
        lines: &[
            "                     .-,.       ",
            "        '-.__.-.          '-.,-'",
            "          \\\\            ",
            "           \\\\               ",
            "            \\\\            ",
            "      .-.    \\\\      .--.   ",
            "      \\\\      \\\\      \\\\    ",
            "       '-'     '-\\ .-'      ",
            "             .-`                ",
            "          .-'                    ",
        ],
    },
];

pub fn by_name(name: &str) -> Option<&'static Logo> {
    LOGOS.iter().find(|l| l.name.eq_ignore_ascii_case(name))
}

pub fn list_names() -> impl Iterator<Item = &'static str> {
    LOGOS.iter().map(|l| l.name)
}