/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/

#include "tablepile.h"
#include "skinbank.h"
#include <QParallelAnimationGroup>
#include "pixmapanimation.h"
#include "roomscene.h"

#include <QTimer>

QList<CardItem *> TablePile::removeCardItems(const QList<int> &card_ids, const CardsMoveStruct &moveInfo)
{
    if (moveInfo.to_place == Player::PlaceSpecial)
        return QList<CardItem *>();
    QList<CardItem *> result;
    _m_mutex_pileCards.lock();
    result = _createCards(card_ids);
    _disperseCards(result, m_cardsDisplayRegion.center(), false);
    _m_mutex_pileCards.unlock();
    return result;
}

QRectF TablePile::boundingRect() const
{
    return m_cardsDisplayRegion;
}

void TablePile::setSize(double width, double height)
{
    m_cardsDisplayRegion = QRect(0, 0, width, height);
    m_numCardsVisible = width / G_COMMON_LAYOUT.m_cardNormalHeight + 1;
    resetTransform();
    setTransform(QTransform::fromTranslate(-width / 2, -height / 2), true);
}

void TablePile::timerEvent(QTimerEvent *)
{
    QList<CardItem *> oldCards;
    _m_mutex_pileCards.lock();
    m_currentTime++;
    foreach (CardItem *toRemove, m_visibleCards) {
        if (m_currentTime - toRemove->m_uiHelper.tablePileClearTimeStamp > S_CLEARANCE_DELAY_BUCKETS) {
            oldCards.append(toRemove);
            m_visibleCards.removeOne(toRemove);
        } else if (m_currentTime > toRemove->m_uiHelper.tablePileClearTimeStamp)
            toRemove->setEnabled(false); // @todo: this is a dirty trick. Use another property in the future
    }

    if (oldCards.isEmpty()) {
        _m_mutex_pileCards.unlock();
        return;
    }

    _fadeOutCardsLocked(oldCards);
    _m_mutex_pileCards.unlock();

    adjustCards();
}

void TablePile::_markClearance(CardItem *item)
{
    if (item->m_uiHelper.tablePileClearTimeStamp > m_currentTime)
        item->m_uiHelper.tablePileClearTimeStamp = m_currentTime;
}

void TablePile::clear()
{
    clear(true);
}

void TablePile::clear(bool delayRequest)
{
    if (m_visibleCards.isEmpty()) return;
    _m_mutex_pileCards.lock();
    // check again since we just gain the lock.
    if (m_visibleCards.isEmpty()) {
        _m_mutex_pileCards.unlock();
        return;
    }

    if (delayRequest) {
        foreach(CardItem *toRemove, m_visibleCards)
            _markClearance(toRemove);
    } else {
        _fadeOutCardsLocked(m_visibleCards);
        m_visibleCards.clear();
    }

    _m_mutex_pileCards.unlock();
}

void TablePile::_fadeOutCardsLocked(const QList<CardItem *> &cards)
{
    QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
    foreach (CardItem *toRemove, cards) {
        toRemove->setZValue(0.0);
        toRemove->setHomeOpacity(0.0);
        toRemove->setHomePos(QPointF(toRemove->homePos().x(), toRemove->homePos().y()));
        toRemove->deleteLater();
        group->addAnimation(toRemove->getGoBackAnimation(true, false, 1000));
    }
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TablePile::showJudgeResult(int cardId, bool takeEffect)
{
    _m_mutex_pileCards.lock();
    CardItem *judgeCard = NULL;
    for (int i = m_visibleCards.size() - 1; i >= 0; i--) {
        CardItem *item = m_visibleCards[i];
        if (item->getCard() && item->getCard()->getId() == cardId && judgeCard == NULL)
            judgeCard = m_visibleCards[i];
    }
    if (judgeCard == NULL)
        judgeCard = _createCard(cardId);
    PixmapAnimation::GetPixmapAnimation(judgeCard, takeEffect ? "judgegood" : "judgebad");
    _m_mutex_pileCards.unlock();
    adjustCards();
}

bool TablePile::_addCardItems(QList<CardItem *> &card_items, const CardsMoveStruct &moveInfo)
{
    if (card_items.isEmpty())
        return false;
    else if (moveInfo.reason.m_reason == CardMoveReason::S_REASON_NATURAL_ENTER) {
        foreach (CardItem *item, card_items) {
            item->deleteLater();
            card_items.clear();
        }
        return false;
    } else if (moveInfo.reason.m_reason == CardMoveReason::S_REASON_SWAP || moveInfo.reason.m_skillName == "luck_card") {
        foreach(CardItem *item, card_items)
            item->hide();
        return false;
    }

    _m_mutex_pileCards.lock();

    QList<CardItem *> cardsToClear;
    for (int i = m_visibleCards.size() - 1; i >= 0; i--) {
        CardItem *item1 = m_visibleCards[i];
        if (item1->getId() == Card::S_UNKNOWN_CARD_ID) continue;
        foreach(CardItem *item2, card_items){
            if (item1->getCard() && item2->getCard() && item1->getCard()->getId() == item2->getCard()->getId()){
                cardsToClear.append(item1);
            }
        }
    }

    foreach(CardItem *item, cardsToClear)
        m_visibleCards.removeOne(item);

    _fadeOutCardsLocked(cardsToClear);

    QPointF rightMostPos = m_cardsDisplayRegion.center();
    if (m_visibleCards.length() > 0) {
        rightMostPos = m_visibleCards.last()->homePos();
        rightMostPos += QPointF(G_COMMON_LAYOUT.m_cardNormalWidth, 0);
    }

    m_visibleCards.append(card_items);

    foreach (CardItem *card_item, card_items) {
        card_item->setHomeOpacity(1.0);
        card_item->showFootnote();
        if (moveInfo.from_place == Player::DrawPile
            || moveInfo.from_place == Player::DrawPileBottom
            || moveInfo.from_place == Player::PlaceJudge
            || moveInfo.from_place == Player::PlaceTable) {
            card_item->setOpacity(0.0);
            card_item->setPos(rightMostPos);
            rightMostPos += QPointF(G_COMMON_LAYOUT.m_cardNormalWidth, 0);
        }
        card_item->m_uiHelper.tablePileClearTimeStamp = INT_MAX;
    }

    _m_mutex_pileCards.unlock();
    adjustCards();
    return false;
}

void TablePile::adjustCards()
{
    if (m_visibleCards.length() == 0) return;
    _disperseCards(m_visibleCards, m_cardsDisplayRegion, Qt::AlignCenter, true, true);
    QParallelAnimationGroup *animation = new QParallelAnimationGroup(this);
    foreach(CardItem *card_item, m_visibleCards)
        animation->addAnimation(card_item->getGoBackAnimation(true));
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

