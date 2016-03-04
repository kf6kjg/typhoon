/** 
 * @file lloutfitgallery.cpp
 * @author Pavlo Kryvych
 * @brief Visual gallery of agent's outfits for My Appearance side panel
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2015, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h" // must be first include
#include "lloutfitgallery.h"

#include <boost/foreach.hpp>

// llcommon
#include "llcommonutils.h"
#include "llvfile.h"

#include "llappearancemgr.h"
#include "lleconomy.h"
#include "llerror.h"
#include "llfilepicker.h"
#include "llfloaterperms.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "lllocalbitmaps.h"
#include "llviewermenufile.h"
#include "llwearableitemslist.h"

static LLPanelInjector<LLOutfitGallery> t_outfit_gallery("outfit_gallery");
static LLOutfitGallery* gOutfitGallery = NULL;

LLOutfitGallery::LLOutfitGallery(const LLOutfitGallery::Params& p)
    : LLOutfitListBase(),
      mTexturesObserver(NULL),
      mOutfitsObserver(NULL),
      mScrollPanel(NULL),
      mGalleryPanel(NULL),
      mGalleryCreated(false),
      mRowCount(0),
      mItemsAddedCount(0),
      mOutfitLinkPending(NULL),
      mRowPanelHeight(p.row_panel_height),
      mVerticalGap(p.vertical_gap),
      mHorizontalGap(p.horizontal_gap),
      mItemWidth(p.item_width),
      mItemHeight(p.item_height),
      mItemHorizontalGap(p.item_horizontal_gap),
      mItemsInRow(p.items_in_row)
{
    mRowPanelWidth = p.row_panel_width_factor * mItemsInRow;
    mGalleryWidth = p.gallery_width_factor * mItemsInRow;
}

LLOutfitGallery::Params::Params()
    : row_panel_height("row_panel_height", 180),
      vertical_gap("vertical_gap", 10),
      horizontal_gap("horizontal_gap", 10),
      item_width("item_width", 150),
      item_height("item_height", 175),
      item_horizontal_gap("item_horizontal_gap", 16),
      items_in_row("items_in_row", 3),
      row_panel_width_factor("row_panel_width_factor", 166),
      gallery_width_factor("gallery_width_factor", 163)
{
    addSynonym(row_panel_height, "row_height");
}

const LLOutfitGallery::Params& LLOutfitGallery::getDefaultParams()
{
    return LLUICtrlFactory::getDefaultParams<LLOutfitGallery>();
}

BOOL LLOutfitGallery::postBuild()
{
    BOOL rv = LLOutfitListBase::postBuild();
    mScrollPanel = getChild<LLScrollContainer>("gallery_scroll_panel");
    mGalleryPanel = getChild<LLPanel>("gallery_panel");
    return rv;
}

void LLOutfitGallery::onOpen(const LLSD& info)
{
    LLOutfitListBase::onOpen(info);
    if (!mGalleryCreated)
    {
        loadPhotos();
        uuid_vec_t cats;
        getCurrentCategories(cats);
        int n = cats.size();
        buildGalleryPanel(n);
        mScrollPanel->addChild(mGalleryPanel);
        for (int i = 0; i < n; i++)
        {
            addToGallery(mOutfitMap[cats[i]]);
        }
        mGalleryCreated = true;
    }
}

LLPanel* LLOutfitGallery::addLastRow()
{
    mRowCount++;
    int row = 0;
    int vgap = mVerticalGap * row;
    LLPanel* result = buildRowPanel(0, row * mRowPanelHeight + vgap);
    mGalleryPanel->addChild(result);
    return result;
}

void LLOutfitGallery::moveRowUp(int row)
{
    moveRow(row, mRowCount - 1 - row + 1);
}

void LLOutfitGallery::moveRowDown(int row)
{
    moveRow(row, mRowCount - 1 - row - 1);
}

void LLOutfitGallery::moveRow(int row, int pos)
{
    int vgap = mVerticalGap * pos;
    moveRowPanel(mRowPanels[row], 0, pos * mRowPanelHeight + vgap);
}

void LLOutfitGallery::removeLastRow()
{
    mRowCount--;
    mGalleryPanel->removeChild(mLastRowPanel);
    mRowPanels.pop_back();
    mLastRowPanel = mRowPanels.back();
}

LLPanel* LLOutfitGallery::addToRow(LLPanel* row_stack, LLOutfitGalleryItem* item, int pos, int hgap)
{
    LLPanel* lpanel = buildItemPanel(pos * mItemWidth + hgap);
    lpanel->addChild(item);
    row_stack->addChild(lpanel);
    mItemPanels.push_back(lpanel);
    return lpanel;
}

void LLOutfitGallery::addToGallery(LLOutfitGalleryItem* item)
{
    mItemsAddedCount++;
    mItemIndexMap[item] = mItemsAddedCount - 1;
    int n = mItemsAddedCount;
    int row_count = (n % mItemsInRow) == 0 ? n / mItemsInRow : n / mItemsInRow + 1;
    int n_prev = n - 1;
    int row_count_prev = (n_prev % mItemsInRow) == 0 ? n_prev / mItemsInRow : n_prev / mItemsInRow + 1;

    bool add_row = row_count != row_count_prev;
    int pos = 0;
    if (add_row)
    {
        for (int i = 0; i < row_count_prev; i++)
        {
            moveRowUp(i);
        }
        mLastRowPanel = addLastRow();
        mRowPanels.push_back(mLastRowPanel);
    }
    pos = (n - 1) % mItemsInRow;
    mItems.push_back(item);
    addToRow(mLastRowPanel, item, pos, mHorizontalGap * pos);
    reshapeGalleryPanel(row_count);
}


void LLOutfitGallery::removeFromGalleryLast(LLOutfitGalleryItem* item)
{
    int n_prev = mItemsAddedCount;
    int n = mItemsAddedCount - 1;
    int row_count = (n % mItemsInRow) == 0 ? n / mItemsInRow : n / mItemsInRow + 1;
    int row_count_prev = (n_prev % mItemsInRow) == 0 ? n_prev / mItemsInRow : n_prev / mItemsInRow + 1;
    mItemsAddedCount--;

    bool remove_row = row_count != row_count_prev;
    removeFromLastRow(mItems[mItemsAddedCount]);
    mItems.pop_back();
    if (remove_row)
    {
        for (int i = 0; i < row_count_prev - 1; i++)
        {
            moveRowDown(i);
        }
        removeLastRow();
    }
    reshapeGalleryPanel(row_count);
}


void LLOutfitGallery::removeFromGalleryMiddle(LLOutfitGalleryItem* item)
{
    int n = mItemIndexMap[item];
    mItemIndexMap.erase(item);
    std::vector<LLOutfitGalleryItem*> saved;
    for (int i = mItemsAddedCount - 1; i > n; i--)
    {
        saved.push_back(mItems[i]);
        removeFromGalleryLast(mItems[i]);
    }
    removeFromGalleryLast(mItems[n]);
    int saved_count = saved.size();
    for (int i = 0; i < saved_count; i++)
    {
        addToGallery(saved.back());
        saved.pop_back();
    }
}

void LLOutfitGallery::removeFromLastRow(LLOutfitGalleryItem* item)
{
    mItemPanels.back()->removeChild(item);
    mLastRowPanel->removeChild(mItemPanels.back());
    mItemPanels.pop_back();
}

LLOutfitGalleryItem* LLOutfitGallery::buildGalleryItem(std::string name)
{
    LLOutfitGalleryItem::Params giparams;
    LLOutfitGalleryItem* gitem = LLUICtrlFactory::create<LLOutfitGalleryItem>(giparams);
    gitem->reshape(mItemWidth, mItemHeight);
    gitem->setVisible(true);
    gitem->setFollowsLeft();
    gitem->setFollowsTop();
    gitem->setOutfitName(name);
    return gitem;
}

void LLOutfitGallery::buildGalleryPanel(int row_count)
{
    LLPanel::Params params;
    mGalleryPanel = LLUICtrlFactory::create<LLPanel>(params);
    reshapeGalleryPanel(row_count);
}

void LLOutfitGallery::reshapeGalleryPanel(int row_count)
{
    int bottom = 0;
    int left = 0;
    int height = row_count * (mRowPanelHeight + mVerticalGap);
    LLRect rect = LLRect(left, bottom + height, left + mGalleryWidth, bottom);
    mGalleryPanel->setRect(rect);
    mGalleryPanel->reshape(mGalleryWidth, height);
    mGalleryPanel->setVisible(true);
    mGalleryPanel->setFollowsLeft();
    mGalleryPanel->setFollowsTop();
}

LLPanel* LLOutfitGallery::buildItemPanel(int left)
{
    LLPanel::Params lpparams;
    int top = 0;
    LLPanel* lpanel = LLUICtrlFactory::create<LLPanel>(lpparams);
    LLRect rect = LLRect(left, top + mItemHeight, left + mItemWidth + mItemHorizontalGap, top);
    lpanel->setRect(rect);
    lpanel->reshape(mItemWidth + mItemHorizontalGap, mItemHeight);
    lpanel->setVisible(true);
    lpanel->setFollowsLeft();
    lpanel->setFollowsTop();
    return lpanel;
}

LLPanel* LLOutfitGallery::buildRowPanel(int left, int bottom)
{
    LLPanel::Params sparams;
    LLPanel* stack = LLUICtrlFactory::create<LLPanel>(sparams);
    moveRowPanel(stack, left, bottom);
    return stack;
}

void LLOutfitGallery::moveRowPanel(LLPanel* stack, int left, int bottom)
{
    LLRect rect = LLRect(left, bottom + mRowPanelHeight, left + mRowPanelWidth, bottom);
    stack->setRect(rect);
    stack->reshape(mRowPanelWidth, mRowPanelHeight);
    stack->setVisible(true);
    stack->setFollowsLeft();
    stack->setFollowsTop();
}

LLOutfitGallery::~LLOutfitGallery()
{
    if (gInventory.containsObserver(mTexturesObserver))
    {
        gInventory.removeObserver(mTexturesObserver);
    }
    delete mTexturesObserver;

    if (gInventory.containsObserver(mOutfitsObserver))
    {
        gInventory.removeObserver(mOutfitsObserver);
    }
    delete mOutfitsObserver;
}

void LLOutfitGallery::setFilterSubString(const std::string& string)
{
    //TODO: Implement filtering

    sFilterSubString = string;
}

void LLOutfitGallery::onHighlightBaseOutfit(LLUUID base_id, LLUUID prev_id)
{
    if (mOutfitMap[base_id])
    {
        mOutfitMap[base_id]->setOutfitWorn(true);
    }
    if (mOutfitMap[prev_id])
    {
        mOutfitMap[prev_id]->setOutfitWorn(false);
    }
}

void LLOutfitGallery::onSetSelectedOutfitByUUID(const LLUUID& outfit_uuid)
{
}

void LLOutfitGallery::getCurrentCategories(uuid_vec_t& vcur)
{
    for (outfit_map_t::const_iterator iter = mOutfitMap.begin();
        iter != mOutfitMap.end();
        iter++)
    {
        if ((*iter).second != NULL)
        {
            vcur.push_back((*iter).first);
        }
    }
}

void LLOutfitGallery::updateAddedCategory(LLUUID cat_id)
{
    LLViewerInventoryCategory *cat = gInventory.getCategory(cat_id);
    if (!cat) return;

    std::string name = cat->getName();
    LLOutfitGalleryItem* item = buildGalleryItem(name);
    mOutfitMap.insert(LLOutfitGallery::outfit_map_value_t(cat_id, item));
    item->setRightMouseDownCallback(boost::bind(&LLOutfitListBase::outfitRightClickCallBack, this,
        _1, _2, _3, cat_id));
    LLWearableItemsList* list = NULL;
    item->setFocusReceivedCallback(boost::bind(&LLOutfitListBase::ChangeOutfitSelection, this, list, cat_id));
    if (mGalleryCreated)
    {
        addToGallery(item);
    }

    LLViewerInventoryCategory* outfit_category = gInventory.getCategory(cat_id);
    if (!outfit_category)
        return;

    if (mOutfitsObserver == NULL)
    {
        mOutfitsObserver = new LLInventoryCategoriesObserver();
        gInventory.addObserver(mOutfitsObserver);
    }

    // Start observing changes in "My Outfits" category.
    mOutfitsObserver->addCategory(cat_id,
        boost::bind(&LLOutfitGallery::refreshOutfit, this, cat_id));

    outfit_category->fetch();
    refreshOutfit(cat_id);
}

void LLOutfitGallery::updateRemovedCategory(LLUUID cat_id)
{
    outfit_map_t::iterator outfits_iter = mOutfitMap.find(cat_id);
    if (outfits_iter != mOutfitMap.end())
    {
        // 0. Remove category from observer.
        mOutfitsObserver->removeCategory(cat_id);

        //const LLUUID& outfit_id = outfits_iter->first;
        LLOutfitGalleryItem* item = outfits_iter->second;

        // An outfit is removed from the list. Do the following:
        // 2. Remove the outfit from selection.
        deselectOutfit(cat_id);

        // 3. Remove category UUID to accordion tab mapping.
        mOutfitMap.erase(outfits_iter);

        // 4. Remove outfit from gallery.
        removeFromGalleryMiddle(item);

        // kill removed item
        if (item != NULL)
        {
            item->die();
        }
    }

}

void LLOutfitGallery::updateChangedCategoryName(LLViewerInventoryCategory *cat, std::string name)
{
    outfit_map_t::iterator outfit_iter = mOutfitMap.find(cat->getUUID());
    if (outfit_iter != mOutfitMap.end())
    {
        // Update name of outfit in gallery
        LLOutfitGalleryItem* item = outfit_iter->second;
        if (item)
        {
            item->setOutfitName(name);
        }
    }
}

void LLOutfitGallery::onOutfitRightClick(LLUICtrl* ctrl, S32 x, S32 y, const LLUUID& cat_id)
{
    if (mOutfitMenu && cat_id.notNull())
    {
        uuid_vec_t selected_uuids;
        selected_uuids.push_back(cat_id);
        mOutfitMenu->show(ctrl, selected_uuids, x, y);
    }
}

void LLOutfitGallery::onChangeOutfitSelection(LLWearableItemsList* list, const LLUUID& category_id)
{
    if (mSelectedOutfitUUID == category_id)
        return;
    if (mOutfitMap[mSelectedOutfitUUID])
    {
        mOutfitMap[mSelectedOutfitUUID]->setSelected(FALSE);
    }
    if (mOutfitMap[category_id])
    {
        mOutfitMap[category_id]->setSelected(TRUE);
    }
}

bool LLOutfitGallery::hasItemSelected()
{
    return false;
}

bool LLOutfitGallery::canWearSelected()
{
    return false;
}

LLOutfitListGearMenuBase* LLOutfitGallery::createGearMenu()
{
    return new LLOutfitGalleryGearMenu(this);
}

static LLDefaultChildRegistry::Register<LLOutfitGalleryItem> r("outfit_gallery_item");

LLOutfitGalleryItem::LLOutfitGalleryItem(const Params& p)
    : LLPanel(p),
    mTexturep(NULL),
    mSelected(false),
    mWorn(false)
{
    buildFromFile("panel_outfit_gallery_item.xml");
}

LLOutfitGalleryItem::~LLOutfitGalleryItem()
{

}

BOOL LLOutfitGalleryItem::postBuild()
{
    setDefaultImage();

    mOutfitNameText = getChild<LLTextBox>("outfit_name");
    mOutfitWornText = getChild<LLTextBox>("outfit_worn_text");
    mFotoBgPanel = getChild<LLPanel>("foto_bg_panel");
    mTextBgPanel = getChild<LLPanel>("text_bg_panel");
    setOutfitWorn(false);
    return TRUE;
}

void LLOutfitGalleryItem::draw()
{
    LLPanel::draw();
    
    // Draw border
    LLUIColor border_color = LLUIColorTable::instance().getColor(mSelected ? "OutfitGalleryItemSelected" : "OutfitGalleryItemUnselected", LLColor4::white);
    LLRect border = getChildView("preview_outfit")->getRect();
    border.mRight = border.mRight + 1;
    gl_rect_2d(border, border_color.get(), FALSE);

    // If the floater is focused, don't apply its alpha to the texture (STORM-677).
    const F32 alpha = getTransparencyType() == TT_ACTIVE ? 1.0f : getCurrentTransparency();
    if (mTexturep)
    {
        LLRect interior = border;
        interior.stretch(-1);

        gl_draw_scaled_image(interior.mLeft - 1, interior.mBottom, interior.getWidth(), interior.getHeight(), mTexturep, UI_VERTEX_COLOR % alpha);

        // Pump the priority
        mTexturep->addTextureStats((F32)(interior.getWidth() * interior.getHeight()));
    }
    
}

void LLOutfitGalleryItem::setOutfitName(std::string name)
{
    mOutfitNameText->setText(name);
}

void LLOutfitGalleryItem::setOutfitWorn(bool value)
{
    mWorn = value;
    LLStringUtil::format_map_t worn_string_args;
    std::string worn_string = getString("worn_string", worn_string_args);
    LLUIColor text_color = LLUIColorTable::instance().getColor(mSelected ? "White" : (mWorn ? "OutfitGalleryItemWorn" : "White"), LLColor4::white);
    mOutfitWornText->setReadOnlyColor(text_color.get());
    mOutfitNameText->setReadOnlyColor(text_color.get());
    mOutfitWornText->setValue(value ? worn_string : "");
}

void LLOutfitGalleryItem::setSelected(bool value)
{
    mSelected = value;
    mTextBgPanel->setBackgroundVisible(value);
    setOutfitWorn(mWorn);
}

BOOL LLOutfitGalleryItem::handleMouseDown(S32 x, S32 y, MASK mask)
{
    setFocus(TRUE);
    return LLUICtrl::handleMouseDown(x, y, mask);
}

void LLOutfitGalleryItem::setImageAssetId(LLUUID image_asset_id)
{
    mTexturep = LLViewerTextureManager::getFetchedTexture(image_asset_id);
    mTexturep->setBoostLevel(LLGLTexture::BOOST_PREVIEW);
}

void LLOutfitGalleryItem::setDefaultImage()
{
    mTexturep = NULL;
}

LLOutfitGalleryGearMenu::LLOutfitGalleryGearMenu(LLOutfitListBase* olist)
    : LLOutfitListGearMenuBase(olist)
{
}

void LLOutfitGalleryGearMenu::onUpdateItemsVisibility()
{
    if (!mMenu) return;
    mMenu->setItemVisible("expand", FALSE);
    mMenu->setItemVisible("collapse", FALSE);
    mMenu->setItemVisible("upload_photo", TRUE);
    mMenu->setItemVisible("load_assets", TRUE);
    LLOutfitListGearMenuBase::onUpdateItemsVisibility();
}

void LLOutfitGalleryGearMenu::onUploadFoto()
{
    LLUUID selected_outfit_id = getSelectedOutfitID();
    LLOutfitGallery* gallery = dynamic_cast<LLOutfitGallery*>(mOutfitList);
    if (gallery && selected_outfit_id.notNull())
    {
        gallery->uploadPhoto(selected_outfit_id);
    }
}

void LLOutfitGallery::loadPhotos()
{
    //Iterate over inventory
    LLUUID textures = gInventory.findCategoryUUIDForType(LLFolderType::FT_TEXTURE);
    LLViewerInventoryCategory* textures_category = gInventory.getCategory(textures);
    if (!textures_category)
        return;
    if (mTexturesObserver == NULL)
    {
        mTexturesObserver = new LLInventoryCategoriesObserver();
        gInventory.addObserver(mTexturesObserver);
    }

    // Start observing changes in "Textures" category.
    mTexturesObserver->addCategory(textures,
        boost::bind(&LLOutfitGallery::refreshTextures, this, textures));
    
    textures_category->fetch();
}

void LLOutfitGallery::refreshOutfit(const LLUUID& category_id)
{
    LLViewerInventoryCategory* category = gInventory.getCategory(category_id);
    {
        bool photo_loaded = false;
        LLInventoryModel::cat_array_t sub_cat_array;
        LLInventoryModel::item_array_t outfit_item_array;
        // Collect all sub-categories of a given category.
        gInventory.collectDescendents(
            category->getUUID(),
            sub_cat_array,
            outfit_item_array,
            LLInventoryModel::EXCLUDE_TRASH);
        BOOST_FOREACH(LLViewerInventoryItem* outfit_item, outfit_item_array)
        {
            LLViewerInventoryItem* linked_item = outfit_item->getLinkedItem();
            if (linked_item->getActualType() == LLAssetType::AT_TEXTURE)
            {
                LLUUID asset_id = linked_item->getAssetUUID();
                mOutfitMap[category_id]->setImageAssetId(asset_id);
                photo_loaded = true;
                break;
            }
            if (!photo_loaded)
            {
                mOutfitMap[category_id]->setDefaultImage();
            }
        }
    }
}

void LLOutfitGallery::refreshTextures(const LLUUID& category_id)
{
    LLInventoryModel::cat_array_t cat_array;
    LLInventoryModel::item_array_t item_array;

    // Collect all sub-categories of a given category.
    LLIsType is_texture(LLAssetType::AT_TEXTURE);
    gInventory.collectDescendentsIf(
        category_id,
        cat_array,
        item_array,
        LLInventoryModel::EXCLUDE_TRASH,
        is_texture);

    //Find texture which contain outfit ID string in name
    LLViewerInventoryItem* photo_upload_item = NULL;
    BOOST_FOREACH(LLViewerInventoryItem* item, item_array)
    {
        std::string name = item->getName();
        if (!mOutfitLinkPending.isNull() && name == mOutfitLinkPending.asString())
        {
            photo_upload_item = item;
            break;
        }
    }

    if (photo_upload_item != NULL)
    {
        LLUUID upload_pending_id = photo_upload_item->getUUID();
        LLInventoryObject* upload_object = gInventory.getObject(upload_pending_id);
        if (!upload_object)
        {
            LL_WARNS() << "LLOutfitGallery::refreshTextures added_object is null!" << LL_ENDL;
        }
        else
        {
            LLViewerInventoryCategory *outfit_cat = gInventory.getCategory(mOutfitLinkPending);
            linkPhotoToOutfit(upload_pending_id, mOutfitLinkPending);

            LLStringUtil::format_map_t photo_string_args;
            photo_string_args["OUTFIT_NAME"] = outfit_cat->getName();
            std::string new_name = getString("outfit_photo_string", photo_string_args);

            LLSD updates;
            updates["name"] = new_name;
            update_inventory_item(upload_pending_id, updates, NULL);

        }
        mOutfitLinkPending.setNull();
    }
}

void LLOutfitGallery::uploadPhoto(LLUUID outfit_id)
{
    outfit_map_t::iterator outfit_it = mOutfitMap.find(outfit_id);
    if (outfit_it == mOutfitMap.end() || outfit_it->first.isNull())
    {
        return;
    }

    LLFilePicker& picker = LLFilePicker::instance();
    if (picker.getOpenFile(LLFilePicker::FFLOAD_IMAGE))
    {
        std::string filename = picker.getFirstFile();
        LLLocalBitmap* unit = new LLLocalBitmap(filename);
        if (unit->getValid())
        {
            S32 expected_upload_cost = LLGlobalEconomy::Singleton::getInstance()->getPriceUpload(); // kinda hack - assumes that unsubclassed LLFloaterNameDesc is only used for uploading chargeable assets, which it is right now (it's only used unsubclassed for the sound upload dialog, and THAT should be a subclass).
            void *nruserdata = NULL;
            nruserdata = (void *)&outfit_id;

            LLViewerInventoryCategory *outfit_cat = gInventory.getCategory(outfit_id);
            if (!outfit_cat) return;

            checkRemovePhoto(outfit_id);
            std::string upload_pending_name = outfit_id.asString();
            LLAssetStorage::LLStoreAssetCallback callback = NULL;
            LLUUID photo_id = upload_new_resource(filename, // file
                upload_pending_name,
                outfit_id.asString(),
                0, LLFolderType::FT_NONE, LLInventoryType::IT_NONE,
                LLFloaterPerms::getNextOwnerPerms("Uploads"),
                LLFloaterPerms::getGroupPerms("Uploads"),
                LLFloaterPerms::getEveryonePerms("Uploads"),
                upload_pending_name, callback, expected_upload_cost, nruserdata);
            mOutfitLinkPending = outfit_id;
        }
    }
}

void LLOutfitGallery::linkPhotoToOutfit(LLUUID photo_id, LLUUID outfit_id)
{
    LLPointer<LLInventoryCallback> cb = new LLUpdateGalleryOnPhotoUpload();
    link_inventory_object(outfit_id, photo_id, cb);
}

bool LLOutfitGallery::checkRemovePhoto(LLUUID outfit_id)
{
    //remove existing photo of outfit from inventory
    texture_map_t::iterator texture_it = mTextureMap.find(outfit_id);
    if (texture_it != mTextureMap.end()) {
        gInventory.removeItem(texture_it->second->getUUID());
        return true;
    }
    return false;
}

void LLOutfitGallery::computeDifferenceOfTextures(
    const LLInventoryModel::item_array_t& vtextures,
    uuid_vec_t& vadded,
    uuid_vec_t& vremoved)
{
    uuid_vec_t vnew;
    // Creating a vector of newly collected texture UUIDs.
    for (LLInventoryModel::item_array_t::const_iterator iter = vtextures.begin();
        iter != vtextures.end();
        iter++)
    {
        vnew.push_back((*iter)->getUUID());
    }

    uuid_vec_t vcur;
    // Creating a vector of currently uploaded texture UUIDs.
    for (texture_map_t::const_iterator iter = mTextureMap.begin();
        iter != mTextureMap.end();
        iter++)
    {
        vcur.push_back((*iter).second->getUUID());
    }

    LLCommonUtils::computeDifference(vnew, vcur, vadded, vremoved);
}