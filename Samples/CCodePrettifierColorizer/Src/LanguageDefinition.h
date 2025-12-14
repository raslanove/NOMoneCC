
/////////////////////////////////////////////////////////
// C language definition in NOMoneCC terms.
// Created by Omar El Sayyed on the 16th of July 2022.
/////////////////////////////////////////////////////////

#pragma once

struct NCC;
typedef struct NCC_Rule NCC_Rule;

void definePreprocessing(struct NCC* ncc);
void defineLanguage(struct NCC* ncc);
NCC_Rule *getRootRule(struct NCC* ncc);