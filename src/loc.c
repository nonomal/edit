// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "loc.h"

#include "arena.h"

#define L(s) {(c8*)s, sizeof(s) - 1}

// TODO: Move this to the OS/platform layer
#define MUI_LANGUAGE_NAME 0x8 // Use ISO language (culture) name convention
__declspec(dllimport) i32 __stdcall GetUserPreferredUILanguages(u32 dwFlags, u32* pulNumLanguages, c16* pwszLanguagesBuffer, u32* pcchLanguagesBuffer);

typedef enum LangId {
    // Base language. It's always the first one.
    LANG_en,

    // Other languages. Sorted alphabetically.
    LANG_de,
    LANG_es,
    LANG_fr,
    LANG_it,
    LANG_ja,
    LANG_ko,
    LANG_pt_br,
    LANG_ru,
    LANG_zh_hans,
    LANG_zh_hant,

    LANG_COUNT,
} LangId;

static const s8 s_lang_lut[LOC_COUNT][LANG_COUNT] = {
    [LOC_Ctrl] = {
        [LANG_en] = L("Ctrl"),
        [LANG_de] = L("Strg"),
        [LANG_es] = L("Ctrl"),
        [LANG_fr] = L("Ctrl"),
        [LANG_it] = L("Ctrl"),
        [LANG_ja] = L("Ctrl"),
        [LANG_ko] = L("Ctrl"),
        [LANG_pt_br] = L("Ctrl"),
        [LANG_ru] = L("Ctrl"),
        [LANG_zh_hans] = L("Ctrl"),
        [LANG_zh_hant] = L("Ctrl"),
    },
    [LOC_Alt] = {
        [LANG_en] = L("Alt"),
        [LANG_de] = L("Alt"),
        [LANG_es] = L("Alt"),
        [LANG_fr] = L("Alt"),
        [LANG_it] = L("Alt"),
        [LANG_ja] = L("Alt"),
        [LANG_ko] = L("Alt"),
        [LANG_pt_br] = L("Alt"),
        [LANG_ru] = L("Alt"),
        [LANG_zh_hans] = L("Alt"),
        [LANG_zh_hant] = L("Alt"),
    },
    [LOC_Shift] = {
        [LANG_en] = L("Shift"),
        [LANG_de] = L("Umschalt"),
        [LANG_es] = L("Mayús"),
        [LANG_fr] = L("Maj"),
        [LANG_it] = L("Maiusc"),
        [LANG_ja] = L("Shift"),
        [LANG_ko] = L("Shift"),
        [LANG_pt_br] = L("Shift"),
        [LANG_ru] = L("Shift"),
        [LANG_zh_hans] = L("Shift"),
        [LANG_zh_hant] = L("Shift"),
    },

    [LOC_File] = {
        [LANG_en] = L("File"),
        [LANG_de] = L("Datei"),
        [LANG_es] = L("Archivo"),
        [LANG_fr] = L("Fichier"),
        [LANG_it] = L("File"),
        [LANG_ja] = L("ファイル"),
        [LANG_ko] = L("파일"),
        [LANG_pt_br] = L("Arquivo"),
        [LANG_ru] = L("Файл"),
        [LANG_zh_hans] = L("文件"),
        [LANG_zh_hant] = L("檔案"),
    },
    [LOC_File_Save] = {
        [LANG_en] = L("Save"),
        [LANG_de] = L("Speichern"),
        [LANG_es] = L("Guardar"),
        [LANG_fr] = L("Enregistrer"),
        [LANG_it] = L("Salva"),
        [LANG_ja] = L("保存"),
        [LANG_ko] = L("저장"),
        [LANG_pt_br] = L("Salvar"),
        [LANG_ru] = L("Сохранить"),
        [LANG_zh_hans] = L("保存"),
        [LANG_zh_hant] = L("儲存"),
    },
    [LOC_File_Save_As] = {
        [LANG_en] = L("Save As"),
        [LANG_de] = L("Speichern unter"),
        [LANG_es] = L("Guardar Como"),
        [LANG_fr] = L("Enregistrer sous"),
        [LANG_it] = L("Salva come"),
        [LANG_ja] = L("名前を付けて保存"),
        [LANG_ko] = L("다른 이름으로 저장"),
        [LANG_pt_br] = L("Salvar Como"),
        [LANG_ru] = L("Сохранить как"),
        [LANG_zh_hans] = L("另存为"),
        [LANG_zh_hant] = L("另存新檔"),
    },
    [LOC_File_Exit] = {
        [LANG_en] = L("Exit"),
        [LANG_de] = L("Beenden"),
        [LANG_es] = L("Salir"),
        [LANG_fr] = L("Quitter"),
        [LANG_it] = L("Esci"),
        [LANG_ja] = L("終了"),
        [LANG_ko] = L("종료"),
        [LANG_pt_br] = L("Sair"),
        [LANG_ru] = L("Выход"),
        [LANG_zh_hans] = L("退出"),
        [LANG_zh_hant] = L("退出"),
    },

    [LOC_Edit] = {
        [LANG_en] = L("Edit"),
        [LANG_de] = L("Bearbeiten"),
        [LANG_es] = L("Editar"),
        [LANG_fr] = L("Éditer"),
        [LANG_it] = L("Modifica"),
        [LANG_ja] = L("編集"),
        [LANG_ko] = L("편집"),
        [LANG_pt_br] = L("Editar"),
        [LANG_ru] = L("Правка"),
        [LANG_zh_hans] = L("编辑"),
        [LANG_zh_hant] = L("編輯"),
    },
    [LOC_Edit_Undo] = {
        [LANG_en] = L("Undo"),
        [LANG_de] = L("Rückgängig"),
        [LANG_es] = L("Deshacer"),
        [LANG_fr] = L("Annuler"),
        [LANG_it] = L("Annulla"),
        [LANG_ja] = L("元に戻す"),
        [LANG_ko] = L("실행 취소"),
        [LANG_pt_br] = L("Desfazer"),
        [LANG_ru] = L("Отменить"),
        [LANG_zh_hans] = L("撤销"),
        [LANG_zh_hant] = L("復原"),
    },
    [LOC_Edit_Redo] = {
        [LANG_en] = L("Redo"),
        [LANG_de] = L("Wiederholen"),
        [LANG_es] = L("Rehacer"),
        [LANG_fr] = L("Rétablir"),
        [LANG_it] = L("Ripeti"),
        [LANG_ja] = L("やり直し"),
        [LANG_ko] = L("다시 실행"),
        [LANG_pt_br] = L("Refazer"),
        [LANG_ru] = L("Повторить"),
        [LANG_zh_hans] = L("重做"),
        [LANG_zh_hant] = L("重做"),
    },
    [LOC_Edit_Cut] = {
        [LANG_en] = L("Cut"),
        [LANG_de] = L("Ausschneiden"),
        [LANG_es] = L("Cortar"),
        [LANG_fr] = L("Couper"),
        [LANG_it] = L("Taglia"),
        [LANG_ja] = L("切り取り"),
        [LANG_ko] = L("잘라내기"),
        [LANG_pt_br] = L("Cortar"),
        [LANG_ru] = L("Вырезать"),
        [LANG_zh_hans] = L("剪切"),
        [LANG_zh_hant] = L("剪下"),
    },
    [LOC_Edit_Copy] = {
        [LANG_en] = L("Copy"),
        [LANG_de] = L("Kopieren"),
        [LANG_es] = L("Copiar"),
        [LANG_fr] = L("Copier"),
        [LANG_it] = L("Copia"),
        [LANG_ja] = L("コピー"),
        [LANG_ko] = L("복사"),
        [LANG_pt_br] = L("Copiar"),
        [LANG_ru] = L("Копировать"),
        [LANG_zh_hans] = L("复制"),
        [LANG_zh_hant] = L("複製"),
    },
    [LOC_Edit_Paste] = {
        [LANG_en] = L("Paste"),
        [LANG_de] = L("Einfügen"),
        [LANG_es] = L("Pegar"),
        [LANG_fr] = L("Coller"),
        [LANG_it] = L("Incolla"),
        [LANG_ja] = L("貼り付け"),
        [LANG_ko] = L("붙여넣기"),
        [LANG_pt_br] = L("Colar"),
        [LANG_ru] = L("Вставить"),
        [LANG_zh_hans] = L("粘贴"),
        [LANG_zh_hant] = L("貼上"),
    },
    [LOC_Edit_Find] = {
        [LANG_en] = L("Find"),
        [LANG_de] = L("Suchen"),
        [LANG_es] = L("Buscar"),
        [LANG_fr] = L("Rechercher"),
        [LANG_it] = L("Trova"),
        [LANG_ja] = L("検索"),
        [LANG_ko] = L("찾기"),
        [LANG_pt_br] = L("Encontrar"),
        [LANG_ru] = L("Найти"),
        [LANG_zh_hans] = L("查找"),
        [LANG_zh_hant] = L("尋找"),
    },
    [LOC_Edit_Replace] = {
        [LANG_en] = L("Replace"),
        [LANG_de] = L("Ersetzen"),
        [LANG_es] = L("Reemplazar"),
        [LANG_fr] = L("Remplacer"),
        [LANG_it] = L("Sostituisci"),
        [LANG_ja] = L("置換"),
        [LANG_ko] = L("바꾸기"),
        [LANG_pt_br] = L("Substituir"),
        [LANG_ru] = L("Заменить"),
        [LANG_zh_hans] = L("替换"),
        [LANG_zh_hant] = L("取代"),
    },

    [LOC_Help] = {
        [LANG_en] = L("Help"),
        [LANG_de] = L("Hilfe"),
        [LANG_es] = L("Ayuda"),
        [LANG_fr] = L("Aide"),
        [LANG_it] = L("Aiuto"),
        [LANG_ja] = L("ヘルプ"),
        [LANG_ko] = L("도움말"),
        [LANG_pt_br] = L("Ajuda"),
        [LANG_ru] = L("Помощь"),
        [LANG_zh_hans] = L("帮助"),
        [LANG_zh_hant] = L("幫助"),
    },
    [LOC_Help_About] = {
        [LANG_en] = L("About"),
        [LANG_de] = L("Über"),
        [LANG_es] = L("Acerca de"),
        [LANG_fr] = L("À propos"),
        [LANG_it] = L("Informazioni"),
        [LANG_ja] = L("情報"),
        [LANG_ko] = L("정보"),
        [LANG_pt_br] = L("Sobre"),
        [LANG_ru] = L("О программе"),
        [LANG_zh_hans] = L("关于"),
        [LANG_zh_hant] = L("關於"),
    },

    [LOC_Exit_Dialog_Title] = {
        [LANG_en] = L("Exit without saving?"),
        [LANG_de] = L("Ohne Speichern beenden?"),
        [LANG_es] = L("¿Salir sin guardar?"),
        [LANG_fr] = L("Quitter sans enregistrer ?"),
        [LANG_it] = L("Uscire senza salvare?"),
        [LANG_ja] = L("保存せずに終了しますか？"),
        [LANG_ko] = L("저장하지 않고 종료하시겠습니까?"),
        [LANG_pt_br] = L("Sair sem salvar?"),
        [LANG_ru] = L("Выйти без сохранения?"),
        [LANG_zh_hans] = L("退出前是否保存？"),
        [LANG_zh_hant] = L("退出不儲存？"),
    },
    [LOC_Exit_Dialog_Yes] = {
        [LANG_en] = L("Yes"),
        [LANG_de] = L("Ja"),
        [LANG_es] = L("Sí"),
        [LANG_fr] = L("Oui"),
        [LANG_it] = L("Sì"),
        [LANG_ja] = L("はい"),
        [LANG_ko] = L("예"),
        [LANG_pt_br] = L("Sim"),
        [LANG_ru] = L("Да"),
        [LANG_zh_hans] = L("是"),
        [LANG_zh_hant] = L("是"),
    },
    [LOC_Exit_Dialog_No] = {
        [LANG_en] = L("No"),
        [LANG_de] = L("Nein"),
        [LANG_es] = L("No"),
        [LANG_fr] = L("Non"),
        [LANG_it] = L("No"),
        [LANG_ja] = L("いいえ"),
        [LANG_ko] = L("아니요"),
        [LANG_pt_br] = L("Não"),
        [LANG_ru] = L("Нет"),
        [LANG_zh_hans] = L("否"),
        [LANG_zh_hant] = L("否"),
    },
};

static int s_lang;

void loc_init()
{
    // Get the user's preferred UI language
    u32 lang_num = 0;
    c16 lang_buf[256] = {0};
    u32 lang_buf_len = array_size(lang_buf);
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &lang_num, lang_buf, &lang_buf_len) || lang_num == 0) {
        return;
    }

    // transform to lowercase
    for (u32 i = 0; i < lang_buf_len; ++i) {
        if (lang_buf[i] >= 'A' && lang_buf[i] <= 'Z') {
            lang_buf[i] += 'a' - 'A';
        }
    }

    // TODO: Iterate through the null-delimited list of language names using wcslen() and convert each to UTF-8. Don't use lang_num
    c16* beg = &lang_buf[0];
    for (; *beg; beg += wcslen(beg)) {
        if (wcsncmp(beg, L"en", 2) == 0) {
            s_lang = LANG_en;
            break;
        }
        if (wcsncmp(beg, L"de", 2) == 0) {
            s_lang = LANG_de;
            break;
        }
        if (wcsncmp(beg, L"es", 2) == 0) {
            s_lang = LANG_es;
            break;
        }
        if (wcsncmp(beg, L"fr", 2) == 0) {
            s_lang = LANG_fr;
            break;
        }
        if (wcsncmp(beg, L"it", 2) == 0) {
            s_lang = LANG_it;
            break;
        }
        if (wcsncmp(beg, L"ja", 2) == 0) {
            s_lang = LANG_ja;
            break;
        }
        if (wcsncmp(beg, L"ko", 2) == 0) {
            s_lang = LANG_ko;
            break;
        }
        if (wcsncmp(beg, L"pt-br", 5) == 0) {
            s_lang = LANG_pt_br;
            break;
        }
        if (wcsncmp(beg, L"ru", 2) == 0) {
            s_lang = LANG_ru;
            break;
        }
        if (wcsncmp(beg, L"zh-hant", 7) == 0) {
            s_lang = LANG_zh_hant;
            break;
        }
        if (wcsncmp(beg, L"zh", 2) == 0) {
            s_lang = LANG_zh_hans;
            break;
        }
    }
}

s8 loc(LocId id)
{
    return s_lang_lut[id][s_lang];
}
